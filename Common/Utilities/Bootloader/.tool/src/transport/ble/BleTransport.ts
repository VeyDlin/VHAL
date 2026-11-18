import type { Characteristic, DisconnectReason, Noble, Peripheral, ServicesAndCharacteristics } from "@stoprocent/noble";
import type { DiscoveredDevice, Transport, TransportFactory } from "../Transport.ts";
import type { Log } from "../../util/Log.ts";
import { TransportError } from "../../util/Errors.ts";
import { parseInteger } from "../../util/Hex.ts";
import type { PacketLink } from "../csp/PacketLink.ts";
import type { CspOptions } from "../csp/CompactStreamingProtocol.ts";
import { CspTransport } from "../csp/CspTransport.ts";


// Full 128-bit forms are what the firmware advertises; the short forms are what noble takes in its calls
const bootloaderServiceUuid: string = "00004242-0000-1000-8000-00805f9b34fb";
const rxCharacteristicUuid: string = "00004243-0000-1000-8000-00805f9b34fb";
const txCharacteristicUuid: string = "00004244-0000-1000-8000-00805f9b34fb";
const bootloaderServiceShort: string = "4242";
const rxCharacteristicShort: string = "4243";
const txCharacteristicShort: string = "4244";

// The nRF8001 characteristic carries at most 20 bytes per write or notification
const blePacketSize: number = 20;
const adapterTimeoutMs: number = 5000;
const disconnectTimeoutMs: number = 3000;

const bleCspOptions: CspOptions = {
    maxPacketSize: blePacketSize,
    fragmentTimeoutMs: 1000,
    maxRetries: 5
};

let nobleInstance: Noble | null = null;


export class BleLink implements PacketLink {
    public readonly maxPacketSize: number = blePacketSize;

    private readonly deviceSelector: string | null;

    private readonly scanTimeoutMs: number;

    private readonly connectTimeoutMs: number;

    private readonly log: Log;

    private noble: Noble | null = null;

    private peripheral: Peripheral | null = null;

    private rxCharacteristic: Characteristic | null = null;

    private txCharacteristic: Characteristic | null = null;

    private packetListener: ((packet: Uint8Array) => void) | null = null;

    private closedListener: ((reason: string) => void) | null = null;


    constructor(
        deviceSelector: string | null,
        scanTimeoutMs: number,
        connectTimeoutMs: number,
        log: Log)
    {
        this.deviceSelector = deviceSelector;
        this.scanTimeoutMs = scanTimeoutMs;
        this.connectTimeoutMs = connectTimeoutMs;
        this.log = log;
    }


    public async open(): Promise<void> {
        if (this.peripheral !== null) {
            return;
        }

        const noble: Noble = await loadNoble();

        try {
            await waitForAdapter(noble);
            const target: Peripheral = await selectPeripheral(noble, this.deviceSelector, this.scanTimeoutMs, this.log);
            await this.connectTo(target);
        } catch (error: unknown) {
            // Without this the noble handle keeps the process alive after the error is reported
            stopNoble(noble);
            throw toTransportError("BLE open failed", error);
        }

        this.noble = noble;
    }


    public async close(): Promise<void> {
        const target: Peripheral | null = this.peripheral;
        const tx: Characteristic | null = this.txCharacteristic;
        const noble: Noble | null = this.noble;

        // Cleared first: the disconnect event that follows is then not reported as a lost link
        this.peripheral = null;
        this.rxCharacteristic = null;
        this.txCharacteristic = null;
        this.noble = null;

        if (target !== null) {
            if (tx !== null) {
                await unsubscribeQuietly(tx);
            }

            await disconnectQuietly(target);
        }

        if (noble !== null) {
            stopNoble(noble);
        }
    }


    public async write(packet: Uint8Array): Promise<void> {
        const characteristic: Characteristic | null = this.rxCharacteristic;

        if (characteristic === null) {
            throw new TransportError("BLE link is not connected");
        }

        if (packet.length > this.maxPacketSize) {
            throw new TransportError(`BLE packet of ${packet.length} bytes exceeds the ${this.maxPacketSize} byte limit`);
        }

        // Write with response (the firmware's RX characteristic) unless it only accepts writes without response
        const withoutResponse: boolean = !characteristic.properties.includes("write")
            && characteristic.properties.includes("writeWithoutResponse");

        try {
            await characteristic.writeAsync(Buffer.from(packet), withoutResponse);
        } catch (error: unknown) {
            throw toTransportError("BLE write failed", error);
        }
    }


    public setPacketListener(listener: (packet: Uint8Array) => void): void {
        this.packetListener = listener;
    }


    public setClosedListener(listener: (reason: string) => void): void {
        this.closedListener = listener;
    }


    private async connectTo(target: Peripheral): Promise<void> {
        this.log.detail(`BLE: connecting to ${describePeripheral(target)}`);

        try {
            await withTimeout(
                target.connectAsync(),
                this.connectTimeoutMs,
                `no connection to ${describePeripheral(target)} within ${this.connectTimeoutMs} ms`
            );

            const discovered: ServicesAndCharacteristics = await withTimeout(
                target.discoverSomeServicesAndCharacteristicsAsync(
                    [bootloaderServiceShort],
                    [rxCharacteristicShort, txCharacteristicShort]
                ),
                this.connectTimeoutMs,
                `service discovery on ${describePeripheral(target)} did not finish within ${this.connectTimeoutMs} ms`
            );
            const rx: Characteristic | undefined = discovered.characteristics.find(
                (characteristic: Characteristic): boolean => sameUuid(characteristic.uuid, rxCharacteristicUuid)
            );
            const tx: Characteristic | undefined = discovered.characteristics.find(
                (characteristic: Characteristic): boolean => sameUuid(characteristic.uuid, txCharacteristicUuid)
            );

            if (rx === undefined || tx === undefined) {
                throw new TransportError("the device does not expose the bootloader RX and TX characteristics");
            }

            tx.on("data", (data: Buffer): void => {
                this.handleNotification(data);
            });
            await withTimeout(
                tx.subscribeAsync(),
                this.connectTimeoutMs,
                `subscribe on ${describePeripheral(target)} did not finish within ${this.connectTimeoutMs} ms`
            );
            target.on("disconnect", (reason: DisconnectReason): void => {
                this.handleDisconnect(target, reason);
            });

            this.peripheral = target;
            this.rxCharacteristic = rx;
            this.txCharacteristic = tx;
            this.log.detail("BLE: subscribed to the TX characteristic");
        } catch (error: unknown) {
            await disconnectQuietly(target);
            throw toTransportError("BLE connection failed", error);
        }
    }


    private handleNotification(data: Buffer): void {
        if (this.peripheral === null || this.packetListener === null) {
            return;
        }

        // Copy: the Buffer belongs to noble and may be reused
        this.packetListener(Uint8Array.from(data));
    }


    private handleDisconnect(target: Peripheral, reason: DisconnectReason): void {
        if (this.peripheral !== target) {
            return;
        }

        this.peripheral = null;
        this.rxCharacteristic = null;
        this.txCharacteristic = null;

        if (this.closedListener !== null) {
            this.closedListener(`BLE link disconnected (${String(reason)})`);
        }
    }
}


export const bleTransport: TransportFactory = {
    name: "ble",
    description: "Bluetooth LE to the bootloader service (characteristics 4243 RX, 4244 TX)",
    options: [
        {
            name: "device",
            description: "device name or address to connect to (default: the only device advertising the bootloader service)"
        },
        {
            name: "scan-timeout",
            description: "scan time in ms while looking for the device",
            defaultValue: "5000"
        },
        {
            name: "connect-timeout",
            description: "connection time limit in ms",
            defaultValue: "10000"
        }
    ],
    create: createBleTransport,
    scan: scanBleDevices
};


function createBleTransport(options: Map<string, string>, log: Log): Transport {
    const link: BleLink = new BleLink(
        readDeviceSelector(options),
        readMilliseconds(options, "scan-timeout", "5000"),
        readMilliseconds(options, "connect-timeout", "10000"),
        log
    );

    return new CspTransport("ble", link, bleCspOptions);
}


async function scanBleDevices(
    options: Map<string, string>,
    durationMs: number,
    log: Log): Promise<DiscoveredDevice[]>
{
    const noble: Noble = await loadNoble();

    try {
        await waitForAdapter(noble);
        log.detail(`BLE: scanning for ${durationMs} ms`);

        const found: Peripheral[] = await scanForBootloaders(noble, durationMs, null);
        return found.map(toDiscoveredDevice);
    } finally {
        stopNoble(noble);
    }
}


function toDiscoveredDevice(peripheral: Peripheral): DiscoveredDevice {
    return {
        id: peripheral.id,
        name: peripheral.advertisement.localName || "(unnamed)",
        rssi: peripheral.rssi
    };
}


function readDeviceSelector(options: Map<string, string>): string | null {
    const value: string | undefined = options.get("device");

    if (value === undefined || value.trim().length === 0) {
        return null;
    }

    return value.trim();
}


function readMilliseconds(options: Map<string, string>, name: string, defaultText: string): number {
    const value: number = parseInteger(options.get(name) ?? defaultText);

    if (value <= 0) {
        throw new TransportError(`--ble-${name} must be a positive number of milliseconds`);
    }

    return value;
}


// The module is loaded here, not at import time, so a missing or broken native binding only affects BLE
async function loadNoble(): Promise<Noble> {
    if (nobleInstance !== null) {
        return nobleInstance;
    }

    try {
        const nobleModule: { default: Noble } = await import("@stoprocent/noble");
        nobleInstance = nobleModule.default;
        return nobleInstance;
    } catch (error: unknown) {
        throw new TransportError(
            `BLE support is not available (${describeError(error)}). ` +
            "Run \"npm install\" in the vboot tool folder (Node 24 or newer) to install @stoprocent/noble."
        );
    }
}


async function waitForAdapter(noble: Noble): Promise<void> {
    try {
        await noble.waitForPoweredOnAsync(adapterTimeoutMs);
    } catch (error: unknown) {
        throw new TransportError(
            `Bluetooth adapter is not powered on within ${adapterTimeoutMs} ms (state: ${noble.state}): ${describeError(error)}`
        );
    }
}


// Scans until the time is up or stopWhen accepts a device. Returns every bootloader device seen.
async function scanForBootloaders(
    noble: Noble,
    durationMs: number,
    stopWhen: ((peripheral: Peripheral) => boolean) | null): Promise<Peripheral[]>
{
    const found: Map<string, Peripheral> = new Map<string, Peripheral>();
    const stopController: AbortController = new AbortController();

    const onDiscover: (peripheral: Peripheral) => void = (peripheral: Peripheral): void => {
        if (!advertisesBootloader(peripheral)) {
            return;
        }

        found.set(peripheral.id, peripheral);

        if (stopWhen !== null && stopWhen(peripheral)) {
            stopController.abort();
        }
    };

    const timer: NodeJS.Timeout = setTimeout((): void => {
        stopController.abort();
    }, durationMs);

    noble.on("discover", onDiscover);

    try {
        await noble.startScanningAsync([bootloaderServiceShort], true);
        await waitForAbort(stopController.signal);
    } catch (error: unknown) {
        throw toTransportError("BLE scan failed", error);
    } finally {
        clearTimeout(timer);
        noble.removeListener("discover", onDiscover);
        await stopScanQuietly(noble);
    }

    return [...found.values()];
}


// Without a selector the scan runs its full time, so that several devices are reported as an error
async function selectPeripheral(
    noble: Noble,
    deviceSelector: string | null,
    scanTimeoutMs: number,
    log: Log): Promise<Peripheral>
{
    if (deviceSelector === null) {
        const found: Peripheral[] = await scanForBootloaders(noble, scanTimeoutMs, null);

        if (found.length === 0) {
            throw new TransportError(
                `no device advertising the bootloader service was found within ${scanTimeoutMs} ms; ` +
                "put the board in bootloader mode, or pass --ble-device"
            );
        }

        if (found.length > 1) {
            throw new TransportError(
                `${found.length} devices advertise the bootloader service, choose one with --ble-device: ${listPeripherals(found)}`
            );
        }

        const only: Peripheral | undefined = found[0];

        if (only === undefined) {
            throw new TransportError("no bootloader device found");
        }

        return only;
    }

    const selector: string = deviceSelector;

    // A name can be shared by several boards, so only an address or id, which is unique, ends the scan early
    const found: Peripheral[] = await scanForBootloaders(
        noble,
        scanTimeoutMs,
        (peripheral: Peripheral): boolean => matchesAddressOrId(peripheral, selector)
    );
    const matches: Peripheral[] = found.filter((peripheral: Peripheral): boolean => matchesSelector(peripheral, selector));

    if (matches.length === 0) {
        throw new TransportError(
            `no bootloader device matching "${selector}" was found within ${scanTimeoutMs} ms`
        );
    }

    if (matches.length > 1) {
        throw new TransportError(
            `${matches.length} devices match "${selector}", use the address to choose one: ${listPeripherals(matches)}`
        );
    }

    const chosen: Peripheral | undefined = matches[0];

    if (chosen === undefined) {
        throw new TransportError(`no bootloader device matching "${selector}" was found`);
    }

    log.detail(`BLE: selected ${describePeripheral(chosen)}`);
    return chosen;
}


function matchesSelector(peripheral: Peripheral, selector: string): boolean {
    const wanted: string = selector.toLowerCase();
    const localName: string = peripheral.advertisement.localName ?? "";

    return matchesAddressOrId(peripheral, selector)
        || localName.toLowerCase() === wanted;
}


function matchesAddressOrId(peripheral: Peripheral, selector: string): boolean {
    const wanted: string = selector.toLowerCase();

    return peripheral.address.toLowerCase() === wanted
        || peripheral.id.toLowerCase() === wanted;
}


function advertisesBootloader(peripheral: Peripheral): boolean {
    return peripheral.advertisement.serviceUuids.some(
        (uuid: string): boolean => sameUuid(uuid, bootloaderServiceUuid)
    );
}


// Compares 16-bit and 128-bit forms of a UUID as lowercase 32 hex digits
function sameUuid(left: string, right: string): boolean {
    return normalizeUuid(left) === normalizeUuid(right);
}


function normalizeUuid(uuid: string): string {
    const compact: string = uuid.toLowerCase().replace(/-/g, "");

    if (compact.length === 4) {
        return `0000${compact}00001000800000805f9b34fb`;
    }

    return compact;
}


function describePeripheral(peripheral: Peripheral): string {
    const name: string = peripheral.advertisement.localName || "(unnamed)";
    return `${name} [${peripheral.address}]`;
}


function listPeripherals(peripherals: Peripheral[]): string {
    return peripherals.map(describePeripheral).join("; ");
}


function waitForAbort(signal: AbortSignal): Promise<void> {
    return new Promise<void>((resolve: () => void): void => {
        if (signal.aborted) {
            resolve();
            return;
        }

        signal.addEventListener("abort", (): void => {
            resolve();
        }, { once: true });
    });
}


// Ends the wait with a TransportError; the underlying work is not cancelled, so callers clean up separately
function withTimeout<T>(work: Promise<T>, timeoutMs: number, message: string): Promise<T> {
    return new Promise<T>((resolve: (value: T) => void, reject: (reason: unknown) => void): void => {
        const timer: NodeJS.Timeout = setTimeout((): void => {
            reject(new TransportError(message));
        }, timeoutMs);

        work.then((value: T): void => {
            clearTimeout(timer);
            resolve(value);
        }, (error: unknown): void => {
            clearTimeout(timer);
            reject(error);
        });
    });
}


async function disconnectQuietly(target: Peripheral): Promise<void> {
    try {
        await withTimeout(target.disconnectAsync(), disconnectTimeoutMs, "disconnect did not finish in time");
    } catch {
        // Already disconnected or still connecting: the adapter drops the link on its own
    }
}


async function unsubscribeQuietly(tx: Characteristic): Promise<void> {
    try {
        await withTimeout(tx.unsubscribeAsync(), disconnectTimeoutMs, "unsubscribe did not finish in time");
    } catch {
        // The link is going down anyway; a failed unsubscribe leaves nothing to clean up
    }
}


// Releases the adapter handle; a stopped noble is not restarted in the same process
function stopNoble(noble: Noble): void {
    try {
        noble.stop();
    } catch {
        // Nothing to release when the bindings never started
    }
}


async function stopScanQuietly(noble: Noble): Promise<void> {
    try {
        await noble.stopScanningAsync();
    } catch {
        // Not scanning any more (the adapter may have ended the scan already)
    }
}


function toTransportError(message: string, error: unknown): TransportError {
    if (error instanceof TransportError) {
        return error;
    }

    return new TransportError(`${message}: ${describeError(error)}`);
}


function describeError(error: unknown): string {
    if (error instanceof Error) {
        return error.message;
    }

    return String(error);
}
