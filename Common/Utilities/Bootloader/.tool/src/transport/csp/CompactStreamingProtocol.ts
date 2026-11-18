import { TransportError, VbootError } from "../../util/Errors.ts";
import type { PacketLink } from "./PacketLink.ts";


// Format v2, mirrors CompactStreamingProtocol.h: typeAndFlags, payloadSize, fragmentIndex, payload, CRC8
const headerSize: number = 3;
const crcSize: number = 1;
const typeMask: number = 0x01;
const packetTypeData: number = 0x00;
const packetTypeAck: number = 0x01;
const lastFragmentFlag: number = 0x02;
const messageIdShift: number = 2;
const messageIdMask: number = 0xfc;
const messageIdModulo: number = 64;
// The device's TX_BUFFER_SIZE: the longest message it can send; a bootloader packet is at most 4 + 255 bytes
const maxMessageSize: number = 512;
const crcInitial: number = 0xff;
const crcPolynomial: number = 0x07;

export const defaultFragmentTimeoutMs: number = 1000;
export const defaultMaxRetries: number = 5;


export interface CspOptions {
    maxPacketSize: number;
    fragmentTimeoutMs: number;
    maxRetries: number;
}


interface OutgoingMessage {
    data: Uint8Array;
    resolve: () => void;
    reject: (error: Error) => void;
}


function crc8(data: Uint8Array): number {
    let crc: number = crcInitial;
    for (const byte of data) {
        crc ^= byte;
        for (let bit: number = 0; bit < 8; bit++) {
            if ((crc & 0x80) !== 0) {
                crc = ((crc << 1) ^ crcPolynomial) & 0xff;
            } else {
                crc = (crc << 1) & 0xff;
            }
        }
    }

    return crc;
}


function makeTypeAndFlags(packetType: number, messageId: number, lastFragment: boolean): number {
    return (packetType & typeMask) | ((messageId << messageIdShift) & messageIdMask) | (lastFragment ? lastFragmentFlag : 0);
}


function encodeFrame(typeAndFlags: number, fragmentIndex: number, payload: Uint8Array): Uint8Array {
    const frame: Uint8Array = new Uint8Array(headerSize + payload.length + crcSize);
    frame[0] = typeAndFlags;
    frame[1] = payload.length;
    frame[2] = fragmentIndex;
    frame.set(payload, headerSize);
    frame[frame.length - 1] = crc8(frame.subarray(0, frame.length - crcSize));

    return frame;
}


function errorText(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}


export class CompactStreamingProtocol {
    private readonly link: PacketLink;


    private readonly options: CspOptions;


    private readonly maxPayloadSize: number;


    private dataListener: ((data: Uint8Array) => void) | null = null;


    private resetListener: (() => void) | null = null;


    private rxBuffer: Uint8Array = new Uint8Array(64);


    private rxLength: number = 0;


    private rxHasLast: boolean = false;


    private rxLastMessageId: number = 0;


    private rxLastIndex: number = 0;


    private rxMessageOpen: boolean = false;


    private rxOpenMessageId: number = 0;


    private rxNextIndex: number = 0;


    private txMessageId: number = 0;


    private txActive: OutgoingMessage | null = null;


    private readonly txQueue: OutgoingMessage[] = [];


    private txCurrentFragment: number = 0;


    private txRetries: number = 0;


    private txTimer: ReturnType<typeof setTimeout> | null = null;


    private lastWriteError: string | null = null;


    public constructor(link: PacketLink, options: CspOptions) {
        const minimumPacketSize: number = headerSize + crcSize + 1;
        if (!Number.isInteger(options.maxPacketSize) || options.maxPacketSize < minimumPacketSize) {
            throw new VbootError(`CSP maxPacketSize must be an integer of at least ${minimumPacketSize}`);
        }

        if (!(options.fragmentTimeoutMs > 0)) {
            throw new VbootError("CSP fragmentTimeoutMs must be positive");
        }

        if (!Number.isInteger(options.maxRetries) || options.maxRetries < 0) {
            throw new VbootError("CSP maxRetries must be an integer of zero or more");
        }

        this.link = link;
        this.options = options;
        this.maxPayloadSize = options.maxPacketSize - headerSize - crcSize;
    }


    // Queues the message behind any message in progress; resolves when every fragment is acknowledged
    public send(message: Uint8Array): Promise<void> {
        return new Promise<void>((resolvePromise: (value: void | PromiseLike<void>) => void, rejectPromise: (reason: unknown) => void): void => {
            if (message.length > maxMessageSize) {
                rejectPromise(new TransportError(`message of ${message.length} bytes, the limit is ${maxMessageSize}`));
                return;
            }

            this.txQueue.push({
                data: message.slice(),
                resolve: (): void => {
                    resolvePromise();
                },
                reject: (error: Error): void => {
                    rejectPromise(error);
                }
            });

            if (this.txActive === null) {
                this.startNextMessage();
            }
        });
    }


    public setDataListener(listener: (data: Uint8Array) => void): void {
        this.dataListener = listener;
    }


    public setResetListener(listener: () => void): void {
        this.resetListener = listener;
    }


    // Called with every packet the link receives; a packet may hold several frames or a part of one
    public handlePacket(packet: Uint8Array): void {
        this.appendReceived(packet);
        this.processIncoming();
    }


    // Drops all state and rejects every message that has not been delivered yet
    public reset(): void {
        this.clearTimer();
        this.rxLength = 0;
        this.rxHasLast = false;
        this.rxMessageOpen = false;
        this.txCurrentFragment = 0;
        this.txRetries = 0;
        this.lastWriteError = null;

        const active: OutgoingMessage | null = this.txActive;
        this.txActive = null;
        const queued: OutgoingMessage[] = this.txQueue.splice(0);

        if (active !== null) {
            active.reject(new TransportError("CSP reset while a message was being sent"));
        }

        for (const message of queued) {
            message.reject(new TransportError("CSP reset while a message was being sent"));
        }
    }


    private appendReceived(packet: Uint8Array): void {
        const required: number = this.rxLength + packet.length;
        if (required > this.rxBuffer.length) {
            const grown: Uint8Array = new Uint8Array(Math.max(required, this.rxBuffer.length * 2));
            grown.set(this.rxBuffer.subarray(0, this.rxLength));
            this.rxBuffer = grown;
        }

        this.rxBuffer.set(packet, this.rxLength);
        this.rxLength = required;
    }


    private consumeReceived(count: number): void {
        this.rxBuffer.copyWithin(0, count, this.rxLength);
        this.rxLength -= count;
    }


    // Frames are checked before they are consumed; a bad frame costs one byte and the scan resumes at the next byte
    private processIncoming(): void {
        while (this.rxLength >= headerSize + crcSize) {
            const payloadSize: number = this.rxBuffer[1] ?? 0;
            if (payloadSize > this.maxPayloadSize) {
                this.consumeReceived(1);
                continue;
            }

            const frameSize: number = headerSize + payloadSize + crcSize;
            if (this.rxLength < frameSize) {
                return;
            }

            const frame: Uint8Array = this.rxBuffer.slice(0, frameSize);
            if (frame[frameSize - crcSize] !== crc8(frame.subarray(0, frameSize - crcSize))) {
                this.consumeReceived(1);
                continue;
            }

            this.consumeReceived(frameSize);
            this.processFrame(frame);
        }
    }


    private processFrame(frame: Uint8Array): void {
        const typeAndFlags: number = frame[0] ?? 0;
        const payloadSize: number = frame[1] ?? 0;
        const fragmentIndex: number = frame[2] ?? 0;
        const messageId: number = (typeAndFlags & messageIdMask) >> messageIdShift;

        if ((typeAndFlags & typeMask) === packetTypeAck) {
            this.handleAck(messageId, fragmentIndex);
            return;
        }

        const lastFragment: boolean = (typeAndFlags & lastFragmentFlag) !== 0;
        this.handleData(messageId, fragmentIndex, lastFragment, frame.subarray(headerSize, headerSize + payloadSize));
    }


    private handleData(messageId: number, fragmentIndex: number, lastFragment: boolean, payload: Uint8Array): void {
        // A retransmission means our ACK was lost: acknowledge again, deliver nothing
        if (this.rxHasLast && messageId === this.rxLastMessageId && fragmentIndex === this.rxLastIndex) {
            this.sendAck(messageId, fragmentIndex);
            return;
        }

        if (fragmentIndex === 0) {
            // The previous message never got its last fragment, so its tail is lost
            if (this.rxMessageOpen) {
                this.notifyReset();
            }

            this.sendAck(messageId, fragmentIndex);
            this.rxMessageOpen = !lastFragment;
            this.rxOpenMessageId = messageId;
            this.rxNextIndex = 1;
        } else {
            // Only the next fragment of the open message is accepted; without an ACK the sender times out
            if (!this.rxMessageOpen || messageId !== this.rxOpenMessageId || fragmentIndex !== this.rxNextIndex) {
                return;
            }

            this.sendAck(messageId, fragmentIndex);
            this.rxNextIndex = (this.rxNextIndex + 1) & 0xff;
            if (lastFragment) {
                this.rxMessageOpen = false;
            }
        }

        this.rxHasLast = true;
        this.rxLastMessageId = messageId;
        this.rxLastIndex = fragmentIndex;

        if (payload.length > 0 && this.dataListener !== null) {
            this.dataListener(payload);
        }
    }


    private handleAck(messageId: number, fragmentIndex: number): void {
        if (this.txActive === null) {
            return;
        }

        // An ACK of an earlier message or fragment is stale and must not advance the current one
        if (messageId !== this.txMessageId || fragmentIndex !== this.txCurrentFragment) {
            return;
        }

        this.txRetries = 0;
        this.lastWriteError = null;
        this.txCurrentFragment++;

        if (this.txCurrentFragment >= this.fragmentCountOf(this.txActive.data)) {
            this.finishActive(null);
            return;
        }

        this.transmitCurrentFragment();
    }


    private sendAck(messageId: number, fragmentIndex: number): void {
        this.writeFrame(encodeFrame(makeTypeAndFlags(packetTypeAck, messageId, false), fragmentIndex, new Uint8Array(0)));
    }


    private startNextMessage(): void {
        let next: OutgoingMessage | undefined = this.txQueue.shift();
        while (next !== undefined) {
            // Like the device, an empty message puts nothing on the wire
            if (next.data.length > 0) {
                this.txMessageId = (this.txMessageId + 1) % messageIdModulo;
                this.txActive = next;
                this.txCurrentFragment = 0;
                this.txRetries = 0;
                this.transmitCurrentFragment();
                return;
            }

            next.resolve();
            next = this.txQueue.shift();
        }
    }


    private transmitCurrentFragment(): void {
        const active: OutgoingMessage | null = this.txActive;
        if (active === null) {
            return;
        }

        const offset: number = this.txCurrentFragment * this.maxPayloadSize;
        const end: number = Math.min(offset + this.maxPayloadSize, active.data.length);
        const lastFragment: boolean = this.txCurrentFragment + 1 === this.fragmentCountOf(active.data);
        const typeAndFlags: number = makeTypeAndFlags(packetTypeData, this.txMessageId, lastFragment);

        this.writeFrame(encodeFrame(typeAndFlags, this.txCurrentFragment, active.data.subarray(offset, end)));
        this.armTimer();
    }


    // A refused write is not fatal: the fragment timer retransmits it like a lost fragment
    private writeFrame(frame: Uint8Array): void {
        try {
            this.link.write(frame).catch((error: unknown): void => {
                this.lastWriteError = errorText(error);
            });
        } catch (error: unknown) {
            this.lastWriteError = errorText(error);
        }
    }


    private armTimer(): void {
        this.clearTimer();
        this.txTimer = setTimeout((): void => {
            this.onFragmentTimeout();
        }, this.options.fragmentTimeoutMs);
    }


    private clearTimer(): void {
        if (this.txTimer !== null) {
            clearTimeout(this.txTimer);
            this.txTimer = null;
        }
    }


    private onFragmentTimeout(): void {
        this.txTimer = null;
        if (this.txActive === null) {
            return;
        }

        if (this.txRetries < this.options.maxRetries) {
            this.txRetries++;
            this.transmitCurrentFragment();
            return;
        }

        const detail: string = this.lastWriteError === null ? "" : `; last write error: ${this.lastWriteError}`;
        this.finishActive(new TransportError(`fragment ${this.txCurrentFragment} was not acknowledged after ${this.options.maxRetries} retries${detail}`));
    }


    private finishActive(failure: TransportError | null): void {
        const active: OutgoingMessage | null = this.txActive;
        this.txActive = null;
        this.clearTimer();

        if (active !== null) {
            if (failure === null) {
                active.resolve();
            } else {
                active.reject(failure);
            }
        }

        this.startNextMessage();
    }


    private fragmentCountOf(data: Uint8Array): number {
        return Math.ceil(data.length / this.maxPayloadSize);
    }


    private notifyReset(): void {
        if (this.resetListener !== null) {
            this.resetListener();
        }
    }
}
