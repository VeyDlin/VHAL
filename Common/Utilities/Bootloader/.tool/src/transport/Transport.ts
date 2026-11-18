import type { Log } from "../util/Log.ts";


export interface Transport {
    readonly name: string;

    open(): Promise<void>;

    close(): Promise<void>;

    // Resolves when the transport has delivered the whole message (CSP: every fragment acknowledged)
    send(message: Uint8Array): Promise<void>;

    setDataListener(listener: (data: Uint8Array) => void): void;

    // Framing was lost: an incomplete message was dropped or the link was closed
    setResetListener(listener: () => void): void;
}


export interface TransportOption {
    name: string;
    description: string;
    defaultValue?: string;
}


export interface DiscoveredDevice {
    id: string;
    name: string;
    rssi?: number;
}


export interface TransportFactory {
    readonly name: string;

    readonly description: string;

    readonly options: TransportOption[];

    create(options: Map<string, string>, log: Log): Transport;

    scan?(options: Map<string, string>, durationMs: number, log: Log): Promise<DiscoveredDevice[]>;
}
