import type { Transport } from "../transport/Transport.ts";
import { concatBytes, u32Bytes } from "../util/Bytes.ts";
import { DeviceError, TimeoutError, TransportError, VbootError } from "../util/Errors.ts";
import type { Log } from "../util/Log.ts";
import {
    Command,
    commandName,
    defaultPacketDataMaxSize,
    encodeCommand,
    encodeFirmwareHeader,
    parseDeviceInfo,
    parseDeviceStatus,
    ResponseParser,
    Status,
    statusName
} from "./Bootloader.ts";
import type { DeviceInfo, DeviceStatus, Response } from "./Bootloader.ts";


export interface ClientOptions {
    commandTimeoutMs: number;
    eraseTimeoutMs: number;
    retries: number;
    log: Log;
}


interface ResponseWaiter {
    sequence: number;
    resolve: (response: Response) => void;
}


interface AttemptResult {
    response: Response | null;
    transportError: TransportError | null;
}


interface Deadline {
    expired: Promise<null>;
    cancel: () => void;
}

const emptyData: Uint8Array = new Uint8Array(0);
const firmwareHeaderSize: number = 8;
const sha256Size: number = 32;
const ivSize: number = 16;


function checkByte(value: number, name: string): void {
    if (!Number.isInteger(value) || value < 0 || value > 255) {
        throw new VbootError(`${name} must be a byte (0..255), got ${value}`);
    }
}


export class BootloaderClient {
    // Largest command body the device accepts; starts at the default until getInfo() reports the device value
    public packetDataMaxSize: number = defaultPacketDataMaxSize;
    private readonly transport: Transport;
    private readonly options: ClientOptions;
    private readonly parser: ResponseParser = new ResponseParser();
    private waiter: ResponseWaiter | null = null;
    private nextSequence: number = 0;
    private queue: Promise<void> = Promise.resolve();


    constructor(
        transport: Transport,
        options: ClientOptions)
    {
        this.transport = transport;
        this.options = options;
    }


    public async open(): Promise<void> {
        await this.transport.open();
        this.transport.setDataListener((data: Uint8Array): void => {
            this.handleData(data);
        });
        // A reset means the framing was lost: a partial response left in the parser would swallow the next one
        this.transport.setResetListener((): void => {
            this.parser.clear();
            this.options.log.detail("transport reset: partial response dropped, pending request is retried");
        });
    }


    public async close(): Promise<void> {
        await this.transport.close();
    }


    public request(
        command: number,
        data: Uint8Array = emptyData,
        acceptStatuses: number[] = [],
        timeoutMs: number = this.options.commandTimeoutMs): Promise<Response>
    {
        return this.serialize((): Promise<Response> => {
            return this.exchange(command, data, acceptStatuses, timeoutMs);
        });
    }


    public async ping(): Promise<void> {
        await this.request(Command.Ping);
    }


    public async getInfo(): Promise<DeviceInfo> {
        const response: Response = await this.request(Command.GetInfo);
        const info: DeviceInfo = parseDeviceInfo(response.data);
        this.packetDataMaxSize = info.packetDataMaxSize;
        return info;
    }


    public async getStatus(): Promise<DeviceStatus> {
        const response: Response = await this.request(Command.GetStatus);
        return parseDeviceStatus(response.data);
    }


    public async erase(): Promise<void> {
        await this.request(Command.Erase, emptyData, [], this.options.eraseTimeoutMs);
    }


    public async setIv(iv: Uint8Array): Promise<void> {
        if (iv.length !== ivSize) {
            throw new VbootError(`IV must be ${ivSize} bytes, got ${iv.length}`);
        }

        await this.request(Command.SetIV, iv);
    }


    public async write(chunk: Uint8Array): Promise<void> {
        if (chunk.length === 0) {
            throw new VbootError("Write chunk is empty");
        }

        await this.request(Command.Write, chunk);
    }


    public async readAt(offset: number): Promise<Uint8Array | null> {
        const response: Response = await this.request(Command.Read, u32Bytes(offset), [Status.EndOfData]);
        if (response.status === Status.EndOfData) {
            return null;
        }

        return response.data;
    }


    public async getHash(): Promise<Uint8Array> {
        const response: Response = await this.request(Command.GetHash);
        if (response.data.length !== sha256Size) {
            throw new VbootError(`GetHash answered ${response.data.length} bytes, expected ${sha256Size}`);
        }

        return response.data;
    }


    // The device checks the whole image against the header here, so the command gets the erase timeout
    public async finalize(crc: number, size: number): Promise<void> {
        await this.request(
            Command.Finalize,
            encodeFirmwareHeader(crc, size),
            [],
            this.options.eraseTimeoutMs
        );
    }


    public async verify(): Promise<boolean> {
        const response: Response = await this.request(
            Command.Verify,
            emptyData,
            [Status.Error],
            this.options.eraseTimeoutMs
        );
        return response.status === Status.Success;
    }


    public async resetWritePosition(): Promise<void> {
        await this.request(Command.ResetWritePos);
    }


    public async start(): Promise<void> {
        await this.requestThatMayJump(Command.Start, this.options.eraseTimeoutMs);
    }


    public async reset(): Promise<void> {
        await this.requestThatMayJump(Command.Reset, this.options.commandTimeoutMs);
    }


    public async unlock(password: Uint8Array): Promise<void> {
        await this.request(Command.UnlockDevice, password);
    }


    public async userDataRead(dataType: number): Promise<Uint8Array> {
        checkByte(dataType, "user data type");
        const response: Response = await this.request(Command.UserData, Uint8Array.of(0, dataType));
        return response.data;
    }


    public async userDataWrite(
        dataType: number,
        data: Uint8Array): Promise<void>
    {
        checkByte(dataType, "user data type");
        await this.request(Command.UserData, concatBytes([Uint8Array.of(1, dataType), data]));
    }


    // The device answers Start and Reset and then jumps, so a lost answer leaves the retries unanswered
    private async requestThatMayJump(
        command: number,
        timeoutMs: number): Promise<void>
    {
        try {
            await this.request(command, emptyData, [], timeoutMs);
        } catch (error: unknown) {
            if (error instanceof TimeoutError) {
                throw new TimeoutError(`${error.message}; the device may already have started the application or rebooted`);
            }

            throw error;
        }
    }


    private serialize<T>(task: () => Promise<T>): Promise<T> {
        const result: Promise<T> = this.queue.then(task);
        this.queue = result.then((): void => undefined, (): void => undefined);
        return result;
    }


    private async exchange(
        command: number,
        data: Uint8Array,
        acceptStatuses: number[],
        timeoutMs: number): Promise<Response>
    {
        // Refused before anything is sent: the device drops an oversized packet and rescans its payload for commands
        if (data.length > this.packetDataMaxSize) {
            throw new VbootError(
                `${commandName(command)} payload is ${data.length} bytes, the device accepts at most ${this.packetDataMaxSize}`
            );
        }

        // Each new request gets its own sequence; the retries below reuse it
        const sequence: number = this.nextSequence;
        this.nextSequence = (this.nextSequence + 1) & 0xFF;
        const packet: Uint8Array = encodeCommand(sequence, command, data);
        const maxAttempts: number = this.options.retries + 1;
        let lastTransportError: TransportError | null = null;

        for (let attempt: number = 1; attempt <= maxAttempts; attempt++) {
            if (attempt > 1) {
                this.options.log.detail(`retrying ${commandName(command)} (sequence ${sequence}), attempt ${attempt} of ${maxAttempts}`);
            }

            const result: AttemptResult = await this.attempt(sequence, command, packet, timeoutMs);
            if (result.response !== null) {
                return this.checkStatus(command, result.response, acceptStatuses);
            }

            lastTransportError = result.transportError;
        }

        if (lastTransportError !== null) {
            throw new TransportError(
                `${commandName(command)} was not delivered after ${maxAttempts} attempts: ${lastTransportError.message}`);
        }

        throw new TimeoutError(`no response to ${commandName(command)} after ${maxAttempts} attempts`);
    }


    private async attempt(
        sequence: number,
        command: number,
        packet: Uint8Array,
        timeoutMs: number): Promise<AttemptResult>
    {
        let received: Promise<Response | null> = this.expectResponse(sequence);
        try {
            await this.transport.send(packet);
        } catch (error: unknown) {
            this.waiter = null;
            if (error instanceof TransportError) {
                return { response: null, transportError: error };
            }

            throw error;
        }

        // The response timeout starts once the transport has delivered the command
        const deadline: Deadline = this.startDeadline(timeoutMs);
        try {
            while (true) {
                const response: Response | null = await Promise.race([received, deadline.expired]);
                if (response === null) {
                    return { response: null, transportError: null };
                }

                // Erase answers Busy while the earlier erase still runs; its real answer follows with the same sequence
                if (response.status === Status.Busy && command === Command.Erase) {
                    received = this.expectResponse(sequence);
                    continue;
                }

                return { response: response, transportError: null };
            }
        } finally {
            deadline.cancel();
            this.waiter = null;
        }
    }


    private expectResponse(sequence: number): Promise<Response> {
        return new Promise<Response>((resolve: (response: Response) => void): void => {
            this.waiter = { sequence: sequence, resolve: resolve };
        });
    }


    private startDeadline(timeoutMs: number): Deadline {
        let timer: ReturnType<typeof setTimeout> | undefined = undefined;
        const expired: Promise<null> = new Promise<null>((resolve: (value: null) => void): void => {
            timer = setTimeout((): void => {
                resolve(null);
            }, timeoutMs);
        });

        return {
            expired: expired,
            cancel: (): void => {
                clearTimeout(timer);
            }
        };
    }


    private checkStatus(
        command: number,
        response: Response,
        acceptStatuses: number[]): Response
    {
        if (response.status === Status.Success || acceptStatuses.includes(response.status)) {
            return response;
        }

        throw new DeviceError(
            command,
            response.status,
            `${commandName(command)} failed: ${statusName(response.status)}`);
    }


    private handleData(data: Uint8Array): void {
        const responses: Response[] = this.parser.push(data);
        for (const response of responses) {
            this.deliver(response);
        }
    }


    private deliver(response: Response): void {
        const waiter: ResponseWaiter | null = this.waiter;
        if (waiter === null || waiter.sequence !== response.sequence) {
            this.options.log.detail(
                `ignored response with sequence ${response.sequence}, status ${statusName(response.status)}`);
            return;
        }

        this.waiter = null;
        waiter.resolve(response);
    }
}
