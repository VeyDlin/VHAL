import { readFileSync } from "node:fs";
import { join } from "node:path";
import { Command, InvalidArgumentError, Option } from "commander";
import { BootloaderClient, type ClientOptions } from "../protocol/BootloaderClient.ts";
import { createRegistry } from "../transport/transports.ts";
import type { Transport, TransportFactory } from "../transport/Transport.ts";
import type { TransportRegistry } from "../transport/TransportRegistry.ts";
import { parseInteger } from "../util/Hex.ts";
import { Log, type LogLevel } from "../util/Log.ts";


export const defaultTransportName: string = "ble";
export const defaultTimeoutMs: number = 3000;
export const defaultEraseTimeoutMs: number = 60000;
export const defaultRetries: number = 3;


// Global options as commander stores them; transport options are added under their generated attribute names
export type GlobalOptions = {
    transport: string;
    timeout: number;
    eraseTimeout: number;
    retries: number;
    verbose?: boolean;
    quiet?: boolean;
} & Record<string, unknown>;


export interface DeviceSettings {
    factory: TransportFactory;
    transportOptions: Map<string, string>;
    clientOptions: ClientOptions;
    log: Log;
}


export type DeviceBody = (client: BootloaderClient, log: Log) => Promise<void>;


export function toolVersion(): string {
    const text: string = readFileSync(join(import.meta.dirname, "..", "..", "package.json"), "utf8");
    const manifest: { version: string } = JSON.parse(text);

    return manifest.version;
}


// Commander attribute name of a transport option, e.g. ("ble", "scan-timeout") -> "bleScanTimeout"
export function transportOptionKey(transportName: string, optionName: string): string {
    return new Option(`--${transportName}-${optionName} <value>`).attributeName();
}


export function parseIntegerArgument(value: string): number {
    try {
        return parseInteger(value);
    } catch (error: unknown) {
        throw new InvalidArgumentError(errorMessage(error));
    }
}


export function parsePositiveIntegerArgument(value: string): number {
    const parsed: number = parseIntegerArgument(value);
    if (parsed <= 0) {
        throw new InvalidArgumentError("expected a value greater than 0");
    }

    return parsed;
}


export function parseByteArgument(value: string): number {
    const parsed: number = parseIntegerArgument(value);
    if (parsed < 0 || parsed > 255) {
        throw new InvalidArgumentError("expected a value from 0 to 255");
    }

    return parsed;
}


export function errorMessage(error: unknown): string {
    if (error instanceof Error) {
        return error.message;
    }

    return String(error);
}


export function reportError(error: unknown, verbose: boolean): void {
    console.error(`error: ${errorMessage(error)}`);
    if (verbose && error instanceof Error && error.stack !== undefined) {
        console.error(error.stack);
    }
}


// Every command runs through here: errors become "error: <message>" and exit code 1
export async function runCommand(command: Command, body: () => Promise<void>): Promise<void> {
    try {
        await body();
    } catch (error: unknown) {
        reportError(error, readGlobals(command).verbose === true);
        process.exitCode = 1;
    }
}


export function logOf(command: Command): Log {
    return new Log(logLevelOf(readGlobals(command)));
}


// Settings for a command that talks to a device, including the transport chosen with --transport
export function readDeviceSettings(command: Command): DeviceSettings {
    const globals: GlobalOptions = readGlobals(command);
    const registry: TransportRegistry = createRegistry();
    const factory: TransportFactory = registry.get(globals.transport);
    const log: Log = new Log(logLevelOf(globals));
    const transportOptions: Map<string, string> = new Map<string, string>();

    for (const option of factory.options) {
        const raw: unknown = globals[transportOptionKey(factory.name, option.name)];
        if (typeof raw === "string") {
            transportOptions.set(option.name, raw);
        }
    }

    const clientOptions: ClientOptions = {
        commandTimeoutMs: globals.timeout,
        eraseTimeoutMs: globals.eraseTimeout,
        retries: globals.retries,
        log: log,
    };

    return {
        factory: factory,
        transportOptions: transportOptions,
        clientOptions: clientOptions,
        log: log,
    };
}


// Opens the client, runs the body and closes the client in finally
export async function withDevice(command: Command, body: DeviceBody): Promise<void> {
    const settings: DeviceSettings = readDeviceSettings(command);
    const transport: Transport = settings.factory.create(settings.transportOptions, settings.log);
    const client: BootloaderClient = new BootloaderClient(transport, settings.clientOptions);

    try {
        await client.open();
        await body(client, settings.log);
    } finally {
        await closeClient(client, settings.log);
    }
}


// A failing close must not hide the result of the command
async function closeClient(client: BootloaderClient, log: Log): Promise<void> {
    try {
        await client.close();
    } catch (error: unknown) {
        log.warn(`closing the transport failed: ${errorMessage(error)}`);
    }
}


// Results the user asked for are printed even with --quiet
export function printLines(lines: string[]): void {
    console.log(lines.join("\n"));
}


export function formatHex32(value: number): string {
    return `0x${value.toString(16).padStart(8, "0")}`;
}


function readGlobals(command: Command): GlobalOptions {
    return command.optsWithGlobals<GlobalOptions>();
}


function logLevelOf(globals: GlobalOptions): LogLevel {
    if (globals.quiet === true) {
        return "quiet";
    }

    if (globals.verbose === true) {
        return "verbose";
    }

    return "normal";
}
