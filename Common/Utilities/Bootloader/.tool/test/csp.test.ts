import assert from "node:assert/strict";
import { describe, it } from "node:test";

import { TransportError } from "../src/util/Errors.ts";
import { CompactStreamingProtocol } from "../src/transport/csp/CompactStreamingProtocol.ts";
import type { CspOptions } from "../src/transport/csp/CompactStreamingProtocol.ts";
import type { PacketLink } from "../src/transport/csp/PacketLink.ts";


const linkPacketSize: number = 20;
const fragmentPayload: number = linkPacketSize - 4;
const typeAck: number = 0x01;
const flagLast: number = 0x02;
// A pending promise must fail its test instead of hanging the whole run
const asyncTestOptions: { timeout: number } = { timeout: 10000 };


interface ParsedFrame {
    isAck: boolean;
    messageId: number;
    last: boolean;
    index: number;
    payload: number[];
    crcOk: boolean;
}


function crc8(data: Uint8Array): number {
    let crc: number = 0xFF;

    for (const byte of data) {
        crc ^= byte;

        for (let bit: number = 0; bit < 8; bit++) {
            if ((crc & 0x80) !== 0) {
                crc = ((crc << 1) ^ 0x07) & 0xFF;
            } else {
                crc = (crc << 1) & 0xFF;
            }
        }
    }

    return crc;
}


function frame(typeAndFlags: number, index: number, payload: number[]): Uint8Array {
    const body: number[] = [typeAndFlags, payload.length, index, ...payload];

    return Uint8Array.from([...body, crc8(Uint8Array.from(body))]);
}


function dataFrame(messageId: number, index: number, last: boolean, payload: number[]): Uint8Array {
    return frame((messageId << 2) | (last ? flagLast : 0), index, payload);
}


function ackFrame(messageId: number, index: number): Uint8Array {
    return frame(typeAck | (messageId << 2), index, []);
}


function parseFrame(packet: Uint8Array): ParsedFrame {
    const typeAndFlags: number = packet[0] ?? 0;
    const payloadSize: number = packet[1] ?? 0;
    const index: number = packet[2] ?? 0;
    const body: Uint8Array = packet.subarray(0, 3 + payloadSize);
    const crcOk: boolean = packet.length === 4 + payloadSize && crc8(body) === packet[3 + payloadSize];

    return {
        isAck: (typeAndFlags & 0x01) === typeAck,
        messageId: (typeAndFlags & 0xFC) >> 2,
        last: (typeAndFlags & flagLast) !== 0,
        index: index,
        payload: Array.from(packet.subarray(3, 3 + payloadSize)),
        crcOk: crcOk
    };
}


function bytesFrom(count: number, seed: number): number[] {
    const result: number[] = [];

    for (let i: number = 0; i < count; i++) {
        result.push((seed + i * 7) & 0xFF);
    }

    return result;
}


function sameBytes(a: Uint8Array, b: Uint8Array): boolean {
    return a.length === b.length && a.every((value: number, index: number): boolean => value === b[index]);
}


async function flush(): Promise<void> {
    await new Promise<void>((resolve: () => void): void => {
        setImmediate(resolve);
    });
}


async function waitUntil(condition: () => boolean, timeoutMs: number): Promise<void> {
    const deadline: number = Date.now() + timeoutMs;

    while (!condition()) {
        if (Date.now() > deadline) {
            throw new Error("condition not reached within " + timeoutMs + " ms");
        }

        await new Promise<void>((resolve: () => void): void => {
            setTimeout(resolve, 5);
        });
    }
}


function createOptions(overrides: Partial<CspOptions>): CspOptions {
    return {
        maxPacketSize: linkPacketSize,
        fragmentTimeoutMs: 1000,
        maxRetries: 5,
        ...overrides
    };
}


class FakePacketLink implements PacketLink {
    public readonly maxPacketSize: number = linkPacketSize;
    public readonly written: Uint8Array[] = [];
    private packetListener: ((packet: Uint8Array) => void) | null = null;
    private closedListener: ((reason: string) => void) | null = null;


    public open(): Promise<void> {
        return Promise.resolve();
    }


    public close(): Promise<void> {
        return Promise.resolve();
    }


    public write(packet: Uint8Array): Promise<void> {
        this.written.push(Uint8Array.from(packet));
        return Promise.resolve();
    }


    public setPacketListener(listener: (packet: Uint8Array) => void): void {
        this.packetListener = listener;
    }


    public setClosedListener(listener: (reason: string) => void): void {
        this.closedListener = listener;
    }
}


describe("CompactStreamingProtocol sender", (): void => {
    it("splits a message into fragments with one id and the last flag on the final one", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const message: Uint8Array = Uint8Array.from(bytesFrom(40, 1));
        const sending: Promise<void> = csp.send(message);

        await flush();
        assert.equal(link.written.length, 1, "stop-and-wait: only the first fragment before any ACK");

        const first: ParsedFrame = parseFrame(link.written[0] ?? new Uint8Array(0));

        assert.equal(first.crcOk, true);
        assert.equal(first.isAck, false);
        assert.equal(first.index, 0);
        assert.equal(first.last, false);
        assert.deepEqual(first.payload, Array.from(message.subarray(0, fragmentPayload)));

        const id: number = first.messageId;

        csp.handlePacket(ackFrame(id, 0));
        await flush();
        assert.equal(link.written.length, 2);

        const second: ParsedFrame = parseFrame(link.written[1] ?? new Uint8Array(0));

        assert.equal(second.index, 1);
        assert.equal(second.messageId, id);
        assert.equal(second.last, false);
        assert.deepEqual(second.payload, Array.from(message.subarray(16, 32)));

        csp.handlePacket(ackFrame(id, 1));
        await flush();
        assert.equal(link.written.length, 3);

        const third: ParsedFrame = parseFrame(link.written[2] ?? new Uint8Array(0));

        assert.equal(third.index, 2);
        assert.equal(third.messageId, id);
        assert.equal(third.last, true);
        assert.deepEqual(third.payload, Array.from(message.subarray(32)));

        csp.handlePacket(ackFrame(id, 2));
        await sending;
    });

    it("marks a one-fragment message as last", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const sending: Promise<void> = csp.send(Uint8Array.from([1, 2, 3, 4, 5]));

        await flush();

        const only: ParsedFrame = parseFrame(link.written[0] ?? new Uint8Array(0));

        assert.equal(only.index, 0);
        assert.equal(only.last, true);
        assert.deepEqual(only.payload, [1, 2, 3, 4, 5]);

        csp.handlePacket(ackFrame(only.messageId, 0));
        await sending;
    });

    it("gives consecutive messages consecutive ids modulo 64", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        let previous: number = -1;

        for (let i: number = 0; i < 70; i++) {
            const sending: Promise<void> = csp.send(Uint8Array.from([i]));

            await flush();

            const sent: ParsedFrame = parseFrame(link.written[i] ?? new Uint8Array(0));

            assert.ok(sent.messageId >= 0 && sent.messageId < 64);

            if (previous >= 0) {
                assert.equal(sent.messageId, (previous + 1) % 64, "message " + i);
            }

            previous = sent.messageId;
            csp.handlePacket(ackFrame(sent.messageId, 0));
            await sending;
        }
    });

    it("ignores a stale ACK of an earlier message", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const first: Promise<void> = csp.send(Uint8Array.from([0xA1]));

        await flush();

        const firstId: number = parseFrame(link.written[0] ?? new Uint8Array(0)).messageId;

        csp.handlePacket(ackFrame(firstId, 0));
        await first;

        const second: Promise<void> = csp.send(Uint8Array.from(bytesFrom(40, 9)));

        await flush();
        assert.equal(link.written.length, 2);

        const secondId: number = parseFrame(link.written[1] ?? new Uint8Array(0)).messageId;

        assert.notEqual(secondId, firstId);

        csp.handlePacket(ackFrame(firstId, 0));
        await flush();
        assert.equal(link.written.length, 2, "stale ACK must not advance the second message");

        csp.handlePacket(ackFrame(secondId, 0));
        await flush();
        assert.equal(link.written.length, 3);
        assert.equal(parseFrame(link.written[2] ?? new Uint8Array(0)).index, 1);

        csp.handlePacket(ackFrame(secondId, 1));
        csp.handlePacket(ackFrame(secondId, 2));
        await second;
    });

    it("retransmits the same fragment after the timeout", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({
            fragmentTimeoutMs: 30,
            maxRetries: 5
        }));
        const sending: Promise<void> = csp.send(Uint8Array.from([7, 8, 9]));

        await waitUntil((): boolean => link.written.length >= 2, 2000);

        const original: Uint8Array = link.written[0] ?? new Uint8Array(0);
        const retry: Uint8Array = link.written[1] ?? new Uint8Array(0);

        assert.equal(sameBytes(original, retry), true, "retransmission must be byte-identical");

        csp.handlePacket(ackFrame(parseFrame(original).messageId, 0));
        await sending;
    });

    it("gives each fragment its own retry budget", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({
            fragmentTimeoutMs: 30,
            maxRetries: 1
        }));
        const sending: Promise<void> = csp.send(Uint8Array.from(bytesFrom(40, 1)));

        // Each fragment loses its first transmission, so each one needs exactly one retry
        await waitUntil((): boolean => link.written.length >= 2, 1000);

        const id: number = parseFrame(link.written[0] ?? new Uint8Array(0)).messageId;

        csp.handlePacket(ackFrame(id, 0));
        await waitUntil((): boolean => link.written.length >= 4, 1000);
        csp.handlePacket(ackFrame(id, 1));
        await waitUntil((): boolean => link.written.length >= 6, 1000);
        csp.handlePacket(ackFrame(id, 2));
        await sending;
    });

    it("fails with TransportError after the retries are used up", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({
            fragmentTimeoutMs: 20,
            maxRetries: 2
        }));

        await assert.rejects(csp.send(Uint8Array.from([1, 2])), TransportError);
        await flush();
        assert.equal(link.written.length, 3, "one transmission plus two retries");
    });

    it("queues a second send until the first one is complete", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const first: Promise<void> = csp.send(Uint8Array.from([0x01]));
        const second: Promise<void> = csp.send(Uint8Array.from([0x02]));

        await flush();
        assert.equal(link.written.length, 1, "the second message must wait");

        const firstId: number = parseFrame(link.written[0] ?? new Uint8Array(0)).messageId;

        csp.handlePacket(ackFrame(firstId, 0));
        await first;
        await flush();
        assert.equal(link.written.length, 2);

        const secondFrame: ParsedFrame = parseFrame(link.written[1] ?? new Uint8Array(0));

        assert.deepEqual(secondFrame.payload, [0x02]);

        csp.handlePacket(ackFrame(secondFrame.messageId, 0));
        await second;
    });

    it("rejects a pending send on reset()", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const pending: Promise<void> = csp.send(Uint8Array.from([0x55]));

        await flush();
        csp.reset();
        await assert.rejects(pending, TransportError);
    });


    it("refuses a message longer than the device's 512-byte limit without sending anything", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));

        await assert.rejects(csp.send(new Uint8Array(513)), TransportError);
        await flush();
        assert.equal(link.written.length, 0);

        const accepted: Promise<void> = csp.send(new Uint8Array(512));
        await flush();
        assert.equal(link.written.length, 1);
        csp.reset();
        await assert.rejects(accepted, TransportError);
    });

    it("encodes a queued message from its own copy, not from the caller's later changes", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const first: Promise<void> = csp.send(Uint8Array.from([0x09]));
        const message: Uint8Array = Uint8Array.from([1, 2, 3]);
        const second: Promise<void> = csp.send(message);

        // The second message is only encoded once the first one is acknowledged
        message[0] = 0xEE;
        await flush();

        const firstId: number = parseFrame(link.written[0] ?? new Uint8Array(0)).messageId;

        csp.handlePacket(ackFrame(firstId, 0));
        await first;
        await flush();
        assert.equal(link.written.length, 2);

        const queued: ParsedFrame = parseFrame(link.written[1] ?? new Uint8Array(0));

        assert.deepEqual(queued.payload, [1, 2, 3]);

        csp.handlePacket(ackFrame(queued.messageId, 0));
        await second;
    });

    it("retransmits the bytes it was given, not the caller's later changes", asyncTestOptions, async (): Promise<void> => {
        const link: FakePacketLink = new FakePacketLink();
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({
            fragmentTimeoutMs: 30,
            maxRetries: 5
        }));
        const message: Uint8Array = Uint8Array.from([1, 2, 3]);
        const sending: Promise<void> = csp.send(message);

        message[0] = 0xEE;
        await waitUntil((): boolean => link.written.length >= 2, 2000);

        const retry: ParsedFrame = parseFrame(link.written[1] ?? new Uint8Array(0));

        assert.deepEqual(retry.payload, [1, 2, 3]);

        csp.handlePacket(ackFrame(retry.messageId, 0));
        await sending;
    });
});


describe("CompactStreamingProtocol receiver", (): void => {
    function createReceiver(link: FakePacketLink): {
        csp: CompactStreamingProtocol;
        events: string[];
    } {
        const csp: CompactStreamingProtocol = new CompactStreamingProtocol(link, createOptions({}));
        const events: string[] = [];

        csp.setDataListener((data: Uint8Array): void => {
            events.push("data:" + Array.from(data).join(","));
        });
        csp.setResetListener((): void => {
            events.push("reset");
        });

        return { csp: csp, events: events };
    }

    it("acknowledges a data fragment with its id and index, last flag clear, no payload", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(5, 0, true, [1, 2, 3]));

        assert.equal(link.written.length, 1);

        const ack: ParsedFrame = parseFrame(link.written[0] ?? new Uint8Array(0));

        assert.equal(ack.crcOk, true);
        assert.equal(ack.isAck, true);
        assert.equal(ack.messageId, 5);
        assert.equal(ack.index, 0);
        assert.equal(ack.last, false);
        assert.deepEqual(ack.payload, []);
        assert.deepEqual(receiver.events, ["data:1,2,3"]);
    });

    it("acknowledges a retransmission again but delivers it once", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(1, 0, true, [9, 9]));
        receiver.csp.handlePacket(dataFrame(1, 0, true, [9, 9]));

        assert.equal(link.written.length, 2);
        assert.equal(sameBytes(link.written[0] ?? new Uint8Array(0), link.written[1] ?? new Uint8Array(0)), true);
        assert.deepEqual(receiver.events, ["data:9,9"]);

        receiver.csp.handlePacket(dataFrame(2, 0, true, [9, 9]));

        assert.equal(link.written.length, 3);
        assert.equal(parseFrame(link.written[2] ?? new Uint8Array(0)).messageId, 2);
        assert.deepEqual(receiver.events, ["data:9,9", "data:9,9"]);
    });

    it("resets the receiver once when a new message starts while one is open, before delivering it", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(1, 0, false, [1]));
        receiver.csp.handlePacket(dataFrame(2, 0, true, [2]));

        assert.deepEqual(receiver.events, ["data:1", "reset", "data:2"]);
    });

    it("does not reset after a message that completed", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(1, 0, true, [1]));
        receiver.csp.handlePacket(dataFrame(2, 0, true, [2]));

        assert.deepEqual(receiver.events, ["data:1", "data:2"]);
    });

    it("delivers and acknowledges in-sequence continuation fragments", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(3, 0, false, bytesFrom(16, 1)));
        receiver.csp.handlePacket(dataFrame(3, 1, false, bytesFrom(16, 2)));
        receiver.csp.handlePacket(dataFrame(3, 2, true, bytesFrom(8, 3)));

        assert.equal(link.written.length, 3);
        assert.equal(parseFrame(link.written[2] ?? new Uint8Array(0)).index, 2);
        assert.equal(receiver.events.length, 3);
        assert.equal(receiver.events[0], "data:" + bytesFrom(16, 1).join(","));
        assert.equal(receiver.events[2], "data:" + bytesFrom(8, 3).join(","));
    });

    it("drops out-of-sequence fragments without ACK or delivery", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(dataFrame(1, 1, true, [7]));
        assert.equal(link.written.length, 0);
        assert.deepEqual(receiver.events, []);

        receiver.csp.handlePacket(dataFrame(1, 0, false, [1]));
        assert.equal(link.written.length, 1);

        receiver.csp.handlePacket(dataFrame(1, 2, true, [3]));
        assert.equal(link.written.length, 1, "index 2 after index 0 must not be ACKed");
        assert.deepEqual(receiver.events, ["data:1"]);
    });

    // A corrupted header with a plausible length holds back the bytes after it until enough data has arrived
    // (the same as the C++ receiver), so a sender's retransmissions are what gets the valid frame through
    it("drops a corrupted frame and delivers the next valid one once enough bytes arrive", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);
        const bad: Uint8Array = Uint8Array.from(dataFrame(4, 0, true, [1, 2]));

        bad[bad.length - 1] = (bad[bad.length - 1] ?? 0) ^ 0xFF;

        const good: Uint8Array = dataFrame(4, 0, true, [5, 6]);

        receiver.csp.handlePacket(bad);
        receiver.csp.handlePacket(good);
        receiver.csp.handlePacket(good);
        receiver.csp.handlePacket(good);

        assert.deepEqual(receiver.events, ["data:5,6"]);
        assert.ok(link.written.length >= 1);
        assert.equal(parseFrame(link.written[0] ?? new Uint8Array(0)).messageId, 4);
    });

    // The one-byte resync is pinned here: a valid frame sharing a packet with a corrupted one waits, as in the C++ receiver
    it("holds back a valid frame that shares a packet with a corrupted one", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);
        const bad: Uint8Array = Uint8Array.from(dataFrame(4, 0, true, [1, 2]));

        bad[bad.length - 1] = (bad[bad.length - 1] ?? 0) ^ 0xFF;

        const good: Uint8Array = dataFrame(4, 0, true, [7, 8]);

        receiver.csp.handlePacket(Uint8Array.from([...bad, ...good]));

        assert.deepEqual(receiver.events, []);
    });

    it("resynchronises after a payload length above the maximum", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(Uint8Array.from([0xFF, 0xFF, 0xFF]));
        receiver.csp.handlePacket(dataFrame(6, 0, true, [0x42]));

        assert.deepEqual(receiver.events, ["data:66"]);
    });

    it("assembles a frame split over two packets", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);
        const whole: Uint8Array = dataFrame(1, 0, true, [1, 2, 3]);

        receiver.csp.handlePacket(whole.subarray(0, 2));
        assert.deepEqual(receiver.events, []);

        receiver.csp.handlePacket(whole.subarray(2));
        assert.deepEqual(receiver.events, ["data:1,2,3"]);
    });

    it("delivers two frames that arrive in one packet, in order", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(Uint8Array.from([
            ...dataFrame(1, 0, true, [1]),
            ...dataFrame(2, 0, true, [2])
        ]));

        assert.deepEqual(receiver.events, ["data:1", "data:2"]);
        assert.equal(link.written.length, 2);
    });

    it("forgets a partial frame on reset()", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);
        const whole: Uint8Array = dataFrame(1, 0, true, [4, 5]);

        receiver.csp.handlePacket(whole.subarray(0, 3));
        receiver.csp.reset();
        receiver.csp.handlePacket(whole);

        assert.deepEqual(receiver.events, ["data:4,5"]);
    });

    it("ignores an ACK when no message is in progress", (): void => {
        const link: FakePacketLink = new FakePacketLink();
        const receiver: { csp: CompactStreamingProtocol; events: string[] } = createReceiver(link);

        receiver.csp.handlePacket(ackFrame(3, 0));

        assert.equal(link.written.length, 0);
        assert.deepEqual(receiver.events, []);
    });
});
