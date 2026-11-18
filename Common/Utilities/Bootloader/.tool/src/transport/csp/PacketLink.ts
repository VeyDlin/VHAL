export interface PacketLink {
    readonly maxPacketSize: number;

    open(): Promise<void>;

    close(): Promise<void>;

    write(packet: Uint8Array): Promise<void>;

    setPacketListener(listener: (packet: Uint8Array) => void): void;

    setClosedListener(listener: (reason: string) => void): void;
}
