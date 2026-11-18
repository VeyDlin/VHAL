import type { Transport } from "../Transport.ts";
import { CompactStreamingProtocol, type CspOptions } from "./CompactStreamingProtocol.ts";
import type { PacketLink } from "./PacketLink.ts";


export class CspTransport implements Transport {
    public readonly name: string;


    private readonly link: PacketLink;


    private readonly protocol: CompactStreamingProtocol;


    private resetListener: (() => void) | null = null;


    public constructor(name: string, link: PacketLink, options: CspOptions) {
        this.name = name;
        this.link = link;
        this.protocol = new CompactStreamingProtocol(link, options);

        this.link.setPacketListener((packet: Uint8Array): void => {
            this.protocol.handlePacket(packet);
        });
        this.link.setClosedListener((): void => {
            this.handleLinkClosed();
        });
        this.protocol.setResetListener((): void => {
            this.notifyReset();
        });
    }


    public async open(): Promise<void> {
        await this.link.open();
    }


    public async close(): Promise<void> {
        this.protocol.reset();
        await this.link.close();
    }


    public send(message: Uint8Array): Promise<void> {
        return this.protocol.send(message);
    }


    public setDataListener(listener: (data: Uint8Array) => void): void {
        this.protocol.setDataListener(listener);
    }


    public setResetListener(listener: () => void): void {
        this.resetListener = listener;
    }


    private handleLinkClosed(): void {
        this.protocol.reset();
        this.notifyReset();
    }


    private notifyReset(): void {
        if (this.resetListener !== null) {
            this.resetListener();
        }
    }
}
