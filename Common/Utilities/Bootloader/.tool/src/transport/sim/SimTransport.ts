import { spawn, type ChildProcessWithoutNullStreams } from "node:child_process";
import { join } from "node:path";
import type { Transport, TransportFactory } from "../Transport.ts";
import type { Log } from "../../util/Log.ts";
import { TransportError } from "../../util/Errors.ts";
import { concatBytes } from "../../util/Bytes.ts";
import type { PacketLink } from "../csp/PacketLink.ts";
import type { CspOptions } from "../csp/CompactStreamingProtocol.ts";
import { CspTransport } from "../csp/CspTransport.ts";


const simPacketSize: number = 20;
const simStopTimeoutMs: number = 2000;

const simCspOptions: CspOptions = {
    maxPacketSize: simPacketSize,
    fragmentTimeoutMs: 1000,
    maxRetries: 5
};


// The simulator's stdin and stdout carry CSP packets framed as [u8 length][bytes], both ways
export class SimLink implements PacketLink {
    public readonly maxPacketSize: number = simPacketSize;

    private readonly exePath: string;

    private readonly exeArgs: string[];

    private readonly log: Log;

    private child: ChildProcessWithoutNullStreams | null = null;

    private readBuffer: Uint8Array = new Uint8Array(0);

    private packetListener: ((packet: Uint8Array) => void) | null = null;

    private closedListener: ((reason: string) => void) | null = null;

    // Why the current simulator stopped, reported by later writes; cleared when a new one starts
    private lostReason: string | null = null;


    constructor(
        exePath: string,
        exeArgs: string[],
        log: Log)
    {
        this.exePath = exePath;
        this.exeArgs = exeArgs;
        this.log = log;
    }


    public async open(): Promise<void> {
        if (this.child !== null) {
            return;
        }

        const child: ChildProcessWithoutNullStreams = spawn(
            this.exePath,
            this.exeArgs,
            {
                stdio: ["pipe", "pipe", "pipe"],
                windowsHide: true
            }
        );

        await waitForSpawn(child, this.exePath);
        this.child = child;
        this.lostReason = null;

        child.stdout.on("data",(chunk: Buffer): void => {
            this.handleBytes(chunk);
        });
        child.stderr.on("data", (chunk: Buffer): void => {
            this.logStderr(chunk);
        });
        child.stdin.on("error", (error: Error): void => {
            this.handleLost(child, `simulator input closed (${error.message})`);
        });
        child.on("error", (error: Error): void => {
            this.handleLost(child, `simulator error (${error.message})`);
        });
        child.on("exit", (code: number | null, signal: NodeJS.Signals | null): void => {
            this.handleLost(child, describeExit(code, signal));
        });
    }


    public async close(): Promise<void> {
        const child: ChildProcessWithoutNullStreams | null = this.child;

        // Cleared first so that the exit caused by closing is not reported as a lost link
        this.child = null;

        if (child === null) {
            return;
        }

        // The simulator ends when its input is closed
        child.stdin.end();
        await waitForExit(child, simStopTimeoutMs);
    }


    public async write(packet: Uint8Array): Promise<void> {
        const child: ChildProcessWithoutNullStreams | null = this.child;

        if (child === null) {
            const reason: string = this.lostReason === null ? "" : ` (${this.lostReason})`;
            throw new TransportError(`the simulator is not running${reason}`);
        }

        if (packet.length > simPacketSize) {
            throw new TransportError(`packet of ${packet.length} bytes exceeds the ${simPacketSize} byte limit`);
        }

        const frame: Uint8Array = new Uint8Array(packet.length + 1);
        frame[0] = packet.length;
        frame.set(packet, 1);

        await new Promise<void>((resolve: () => void, reject: (reason: Error) => void): void => {
            child.stdin.write(frame, (error: Error | null | undefined): void => {
                if (error) {
                    reject(new TransportError(`writing to the simulator failed: ${error.message}`));
                    return;
                }

                resolve();
            });
        });
    }


    public setPacketListener(listener: (packet: Uint8Array) => void): void {
        this.packetListener = listener;
    }


    public setClosedListener(listener: (reason: string) => void): void {
        this.closedListener = listener;
    }


    private handleBytes(chunk: Uint8Array): void {
        this.readBuffer = concatBytes([this.readBuffer, chunk]);

        while (this.readBuffer.length > 0) {
            const length: number = this.readBuffer[0] ?? 0;

            if (this.readBuffer.length < length + 1) {
                return;
            }

            const packet: Uint8Array = this.readBuffer.slice(1, length + 1);
            this.readBuffer = this.readBuffer.slice(length + 1);

            if (this.packetListener !== null) {
                this.packetListener(packet);
            }
        }
    }


    private logStderr(chunk: Buffer): void {
        const lines: string[] = chunk.toString("utf8").split(/\r?\n/);

        for (const line of lines) {
            if (line.length > 0) {
                this.log.detail(`sim: ${line}`);
            }
        }
    }


    // Events of a child that is no longer the current one (after close) are ignored
    private handleLost(child: ChildProcessWithoutNullStreams, reason: string): void {
        if (this.child !== child) {
            return;
        }

        this.child = null;
        this.lostReason = reason;

        if (this.closedListener !== null) {
            this.closedListener(reason);
        }
    }
}


export const simTransport: TransportFactory = {
    name: "sim",
    description: "Device simulator built from the bootloader sources (.test/build), CSP over its stdin and stdout",
    options: [
        {
            name: "exe",
            description: "simulator executable (default: .test/build/vboot-sim, vboot-sim.exe on Windows, in the module folder)"
        },
        {
            name: "args",
            description: "extra simulator arguments, split on spaces; a value that starts with - needs =, e.g. --sim-args=\"--cipher aes-256-ctr --key <hex>\""
        }
    ],
    create: createSimTransport
};


function createSimTransport(options: Map<string, string>, log: Log): Transport {
    const exeOption: string = options.get("exe") ?? "";
    const exePath: string = exeOption.length > 0 ? exeOption : defaultSimExePath();
    const exeArgs: string[] = splitArgs(options.get("args") ?? "");

    return new CspTransport("sim", new SimLink(exePath, exeArgs, log), simCspOptions);
}


function defaultSimExePath(): string {
    const fileName: string = process.platform === "win32" ? "vboot-sim.exe" : "vboot-sim";

    // This file lives in <tool>/src/transport/sim
    return join(import.meta.dirname, "..", "..", "..", "..", ".test", "build", fileName);
}


function splitArgs(text: string): string[] {
    return text.split(/\s+/).filter((part: string): boolean => part.length > 0);
}


function waitForSpawn(child: ChildProcessWithoutNullStreams, exePath: string): Promise<void> {
    return new Promise<void>((resolve: () => void, reject: (reason: Error) => void): void => {
        child.once("spawn", (): void => {
            resolve();
        });
        child.once("error", (error: Error): void => {
            reject(new TransportError(
                `cannot start the simulator ${exePath} (${error.message}); build it with "npm run sim:build" in the tool folder`
            ));
        });
    });
}


function waitForExit(child: ChildProcessWithoutNullStreams, timeoutMs: number): Promise<void> {
    return new Promise<void>((resolve: () => void): void => {
        if (child.exitCode !== null || child.signalCode !== null) {
            resolve();
            return;
        }

        const timer: NodeJS.Timeout = setTimeout((): void => {
            child.kill();
            resolve();
        }, timeoutMs);

        child.once("exit", (): void => {
            clearTimeout(timer);
            resolve();
        });
    });
}


function describeExit(code: number | null, signal: NodeJS.Signals | null): string {
    if (signal !== null) {
        return `simulator stopped by ${signal}`;
    }

    return `simulator exited with code ${code ?? -1}`;
}
