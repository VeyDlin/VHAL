import assert from "node:assert/strict";
import { describe, it } from "node:test";

import { BootloaderClient } from "../src/protocol/BootloaderClient.ts";
import type { ClientOptions } from "../src/protocol/BootloaderClient.ts";
import { flash } from "../src/protocol/FlashProcedure.ts";
import type { FlashSource } from "../src/protocol/FlashProcedure.ts";
import { Command, responseHeader, Status } from "../src/protocol/Bootloader.ts";
import type { DeviceInfo, DeviceStatus, Response as BootResponse } from "../src/protocol/Bootloader.ts";
import type { Transport } from "../src/transport/Transport.ts";
import { DeviceError, TimeoutError, TransportError, VbootError } from "../src/util/Errors.ts";
import { Log } from "../src/util/Log.ts";


// A pending promise must fail its test instead of hanging the whole run
const asyncTestOptions: { timeout: number } = { timeout: 10000 };


function responseFrame(sequence: number, status: number, data: number[]): Uint8Array {
    return Uint8Array.from([responseHeader, sequence, status, data.length, ...data]);
}


function answerWith(transport: FakeTransport, status: number, data: number[]): (message: Uint8Array) => void {
    return (message: Uint8Array): void => {
        const sequence: number = message[1] ?? 0;

        setImmediate((): void => {
            transport.deliver(responseFrame(sequence, status, data));
        });
    };
}


function clientOptions(overrides: Partial<ClientOptions>): ClientOptions {
    return {
        commandTimeoutMs: 200,
        eraseTimeoutMs: 2000,
        retries: 2,
        log: new Log("quiet"),
        ...overrides
    };
}


function listOf(data: Uint8Array): number[] {
    return Array.from(data);
}


// packetDataMax 0 means the device uses the default packet data size; cipherId 3 is aes-ctr
function deviceInfoPayload(packetDataMax: number = 0, cipherId: number = 3): number[] {
    const data: Uint8Array = new Uint8Array(24);
    const view: DataView = new DataView(data.buffer);

    view.setBigUint64(0, 0x00010000n, true);
    view.setUint32(8, 20, true);
    view.setUint32(12, 0x08020000, true);
    view.setUint32(16, 0x40000, true);
    data[22] = packetDataMax;
    data[23] = cipherId;

    return listOf(data);
}


class FakeTransport implements Transport {
    public readonly name: string = "fake";
    public readonly calls: Uint8Array[] = [];
    public readonly failures: Error[] = [];
    public sendDelayMs: number = 0;
    public onSend: ((message: Uint8Array) => void) | null = null;
    public openCount: number = 0;
    public closeCount: number = 0;
    private dataListener: ((data: Uint8Array) => void) | null = null;
    private resetListener: (() => void) | null = null;


    public open(): Promise<void> {
        this.openCount++;
        return Promise.resolve();
    }


    public close(): Promise<void> {
        this.closeCount++;
        return Promise.resolve();
    }


    public send(message: Uint8Array): Promise<void> {
        const copy: Uint8Array = Uint8Array.from(message);
        const failure: Error | undefined = this.failures.shift();

        this.calls.push(copy);

        return new Promise<void>((resolve: () => void, reject: (reason: Error) => void): void => {
            setTimeout((): void => {
                if (failure !== undefined) {
                    reject(failure);
                    return;
                }

                resolve();
                this.onSend?.(copy);
            }, this.sendDelayMs);
        });
    }


    public setDataListener(listener: (data: Uint8Array) => void): void {
        this.dataListener = listener;
    }


    public setResetListener(listener: () => void): void {
        this.resetListener = listener;
    }


    public deliver(data: Uint8Array): void {
        this.dataListener?.(data);
    }
}


async function settle(): Promise<void> {
    await new Promise<void>((resolve: () => void): void => {
        setImmediate(resolve);
    });
}


describe("BootloaderClient transport and retries", (): void => {
    it("sends the command packet and resolves on the matching response", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.ping();

        assert.equal(transport.openCount, 1);
        assert.equal(transport.calls.length, 1);

        const packet: number[] = listOf(transport.calls[0] ?? new Uint8Array(0));

        assert.equal(packet[0], 0xAA);
        assert.equal(packet[2], Command.Ping);
        assert.equal(packet[3], 0);
        assert.equal(packet.length, 4);

        await client.close();
        assert.equal(transport.closeCount, 1);
    });

    it("uses a new sequence per request and wraps it as a byte", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();

        for (let i: number = 0; i < 258; i++) {
            await client.ping();
        }

        const sequences: number[] = transport.calls.map((packet: Uint8Array): number => packet[1] ?? -1);

        for (let i: number = 1; i < sequences.length; i++) {
            assert.equal(sequences[i], ((sequences[i - 1] ?? 0) + 1) & 0xFF, "request " + i);
        }
    });

    it("sends the same packet again on timeout, then raises TimeoutError", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({
            commandTimeoutMs: 30,
            retries: 2
        }));

        await client.open();
        await assert.rejects(client.ping(), TimeoutError);

        assert.equal(transport.calls.length, 3, "one attempt plus two retries");
        assert.equal(listOf(transport.calls[0] ?? new Uint8Array(0)).join(","), listOf(transport.calls[2] ?? new Uint8Array(0)).join(","));
    });

    it("succeeds when the answer comes to a retry of the same packet", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({
            commandTimeoutMs: 30,
            retries: 3
        }));
        let attempts: number = 0;

        transport.onSend = (message: Uint8Array): void => {
            attempts++;

            if (attempts === 2) {
                setImmediate((): void => {
                    transport.deliver(responseFrame(message[1] ?? 0, Status.Success, []));
                });
            }
        };
        await client.open();
        await client.ping();

        assert.equal(transport.calls.length, 2);
        assert.equal(listOf(transport.calls[0] ?? new Uint8Array(0)).join(","), listOf(transport.calls[1] ?? new Uint8Array(0)).join(","));
    });

    it("starts the response timeout when send() has resolved, not when it was called", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({
            commandTimeoutMs: 60,
            retries: 0
        }));

        transport.sendDelayMs = 100;
        transport.onSend = (message: Uint8Array): void => {
            setTimeout((): void => {
                transport.deliver(responseFrame(message[1] ?? 0, Status.Success, []));
            }, 10);
        };
        await client.open();
        await client.ping();

        assert.equal(transport.calls.length, 1);
    });

    it("counts a TransportError from send() as a failed attempt and retries the same packet", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({ retries: 2 }));

        transport.failures.push(new TransportError("link lost"));
        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.ping();

        assert.equal(transport.calls.length, 2);
        assert.equal(listOf(transport.calls[0] ?? new Uint8Array(0)).join(","), listOf(transport.calls[1] ?? new Uint8Array(0)).join(","));
    });

    it("ignores a response with another sequence and waits for its own", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = (message: Uint8Array): void => {
            const sequence: number = message[1] ?? 0;

            setImmediate((): void => {
                transport.deliver(responseFrame((sequence + 1) & 0xFF, Status.Error, []));
                transport.deliver(responseFrame(sequence, Status.Success, []));
            });
        };
        await client.open();
        await client.ping();
        assert.equal(transport.calls.length, 1, "the stray response must not cause a retry");
    });

    it("reassembles a response that arrives in several chunks", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = (message: Uint8Array): void => {
            const frame: Uint8Array = responseFrame(message[1] ?? 0, Status.Success, [1, 2, 3]);

            setImmediate((): void => {
                transport.deliver(frame.subarray(0, 3));
                transport.deliver(frame.subarray(3));
            });
        };
        await client.open();

        const response: BootResponse = await client.request(Command.Ping, undefined, []);

        assert.deepEqual(listOf(response.data), [1, 2, 3]);
    });

    it("keeps requests serialised: the next one is sent only after the answer", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        await client.open();

        const first: Promise<void> = client.ping();
        const second: Promise<void> = client.ping();

        await settle();
        assert.equal(transport.calls.length, 1, "second request must wait for the first answer");

        const firstSequence: number = transport.calls[0]?.[1] ?? 0;

        transport.deliver(responseFrame(firstSequence, Status.Success, []));
        await first;
        await settle();
        assert.equal(transport.calls.length, 2);

        const secondSequence: number = transport.calls[1]?.[1] ?? 0;

        transport.deliver(responseFrame(secondSequence, Status.Success, []));
        await second;
    });
});


describe("BootloaderClient status handling", (): void => {
    it("raises DeviceError with the command and status for a failure answer", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.InvalidParameters, []);
        await client.open();
        await assert.rejects(client.ping(), (error: unknown): boolean => {
            return error instanceof DeviceError && error.command === Command.Ping && error.status === Status.InvalidParameters;
        });
    });

    it("lets statuses listed in acceptStatuses through as a response", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Error, []);
        await client.open();

        const response: BootResponse = await client.request(Command.Verify, undefined, [Status.Error]);

        assert.equal(response.status, Status.Error);
    });

    it("maps verify(): Success to true and Error to false", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        await client.open();

        transport.onSend = answerWith(transport, Status.Success, []);
        assert.equal(await client.verify(), true);

        transport.onSend = answerWith(transport, Status.Error, []);
        assert.equal(await client.verify(), false);
    });
});


describe("BootloaderClient commands", (): void => {
    it("erase waits for up to eraseTimeoutMs", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({
            commandTimeoutMs: 20,
            eraseTimeoutMs: 1000,
            retries: 0
        }));

        transport.onSend = (message: Uint8Array): void => {
            setTimeout((): void => {
                transport.deliver(responseFrame(message[1] ?? 0, Status.Success, []));
            }, 100);
        };
        await client.open();
        await client.erase();

        assert.equal(transport.calls.length, 1);
        assert.equal(transport.calls[0]?.[2], Command.Erase);
    });

    it("erase takes the real answer after a retry is answered Busy with the same sequence", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({
            commandTimeoutMs: 50,
            eraseTimeoutMs: 100,
            retries: 1
        }));

        // The first attempt gets no answer; the retry is answered Busy first and Success after that
        transport.onSend = (message: Uint8Array): void => {
            if (transport.calls.length !== 2) {
                return;
            }

            const sequence: number = message[1] ?? 0;

            setTimeout((): void => {
                transport.deliver(responseFrame(sequence, Status.Busy, []));
            }, 5);
            setTimeout((): void => {
                transport.deliver(responseFrame(sequence, Status.Success, []));
            }, 60);
        };
        await client.open();
        await client.erase();

        assert.equal(transport.calls.length, 2);
        assert.equal(transport.calls[0]?.[1], transport.calls[1]?.[1]);
    });

    it("getInfo parses the device information", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, deviceInfoPayload());
        await client.open();

        const info: DeviceInfo = await client.getInfo();

        assert.equal(info.memoryStart, 0x08020000);
        assert.equal(info.cipher, "aes-ctr");
        assert.equal(info.packetDataMaxSize, 240);
    });

    it("getStatus parses the device status", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));
        const payload: number[] = [2, 0, 0, 0, 0x34, 0x12, 0, 0];

        transport.onSend = answerWith(transport, Status.Success, payload);
        await client.open();

        const status: DeviceStatus = await client.getStatus();

        assert.equal(status.state, "processing");
        assert.equal(status.bytesReceived, 0x1234);
    });

    it("setIv sends SetIV with the 16 bytes", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));
        const iv: Uint8Array = Uint8Array.from({ length: 16 }, (ignored: unknown, index: number): number => index + 1);

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.setIv(iv);

        const packet: number[] = listOf(transport.calls[0] ?? new Uint8Array(0));

        assert.equal(packet[2], Command.SetIV);
        assert.equal(packet[3], 16);
        assert.deepEqual(packet.slice(4), listOf(iv));
    });

    it("write sends the chunk as the command data", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));
        const chunk: Uint8Array = Uint8Array.from([0xDE, 0xAD, 0xBE, 0xEF]);

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.write(chunk);

        assert.deepEqual(listOf(transport.calls[0] ?? new Uint8Array(0)).slice(2), [Command.Write, 4, 0xDE, 0xAD, 0xBE, 0xEF]);
    });

    it("readAt sends a 4-byte little-endian offset and returns the data", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, [9, 8, 7]);
        await client.open();

        const data: Uint8Array | null = await client.readAt(0x01020304);

        assert.deepEqual(listOf(transport.calls[0] ?? new Uint8Array(0)).slice(2), [Command.Read, 4, 0x04, 0x03, 0x02, 0x01]);
        assert.deepEqual(data === null ? null : listOf(data), [9, 8, 7]);
    });

    it("readAt returns null at EndOfData", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.EndOfData, []);
        await client.open();
        assert.equal(await client.readAt(600), null);
    });

    it("readAt raises DeviceError for other failures", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Protected, []);
        await client.open();
        await assert.rejects(client.readAt(0), DeviceError);
    });

    it("getHash returns the 32-byte answer", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));
        const hash: number[] = Array.from({ length: 32 }, (ignored: unknown, index: number): number => index);

        transport.onSend = answerWith(transport, Status.Success, hash);
        await client.open();

        const result: Uint8Array = await client.getHash();

        assert.deepEqual(listOf(result), hash);
    });

    it("finalize sends crc32 and size as two little-endian u32 values", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.finalize(0xCBF43926, 0x1234);

        assert.deepEqual(
            listOf(transport.calls[0] ?? new Uint8Array(0)).slice(2),
            [Command.Finalize, 8, 0x26, 0x39, 0xF4, 0xCB, 0x34, 0x12, 0x00, 0x00]
        );
    });

    it("resetWritePosition, start and reset use their command codes", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.resetWritePosition();
        await client.start();
        await client.reset();

        assert.deepEqual(transport.calls.map((packet: Uint8Array): number => packet[2] ?? -1), [
            Command.ResetWritePos,
            Command.Start,
            Command.Reset
        ]);
    });

    it("unlock sends the password", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));
        const password: Uint8Array = Uint8Array.from({ length: 16 }, (ignored: unknown, index: number): number => 0xA0 + index);

        transport.onSend = answerWith(transport, Status.Success, []);
        await client.open();
        await client.unlock(password);

        const packet: number[] = listOf(transport.calls[0] ?? new Uint8Array(0));

        assert.equal(packet[2], Command.UnlockDevice);
        assert.deepEqual(packet.slice(4), listOf(password));
    });

    it("userDataRead and userDataWrite prefix the operation and data type", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, [5, 6]);
        await client.open();

        const read: Uint8Array = await client.userDataRead(0x07);

        assert.deepEqual(listOf(transport.calls[0] ?? new Uint8Array(0)).slice(2), [Command.UserData, 2, 0, 0x07]);
        assert.deepEqual(listOf(read), [5, 6]);

        await client.userDataWrite(0x07, Uint8Array.from([1, 2, 3]));
        assert.deepEqual(listOf(transport.calls[1] ?? new Uint8Array(0)).slice(2), [Command.UserData, 5, 1, 0x07, 1, 2, 3]);
    });

    it("userDataWrite refuses a body above the default packet size before sending", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        await client.open();

        // The body is the operation and data type plus the data, so 239 data bytes make 241 bytes
        await assert.rejects(client.userDataWrite(0x07, new Uint8Array(239)), VbootError);
        assert.equal(transport.calls.length, 0);
    });

    it("userDataWrite uses the packet size that getInfo reported", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = new FakeTransport();
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        transport.onSend = answerWith(transport, Status.Success, deviceInfoPayload(64));
        await client.open();
        await client.getInfo();

        transport.onSend = answerWith(transport, Status.Success, []);

        // A 64-byte body is the device limit; a 65-byte body must not reach the wire
        await client.userDataWrite(0x07, new Uint8Array(62));
        assert.equal(transport.calls.length, 2);

        await assert.rejects(client.userDataWrite(0x07, new Uint8Array(63)), VbootError);
        assert.equal(transport.calls.length, 2);
    });
});


describe("flash chunk size", (): void => {
    const plainFlash: FlashSource = {
        payload: new Uint8Array(64).fill(0x5A),
        imageCrc32: 0,
        imageSize: 64,
        iv: null,
        cipher: "none-or-unknown",
        baseAddress: null
    };

    // Answers Ping and GetInfo only; any other command is recorded but gets no answer
    function infoOnlyTransport(packetDataMax: number): FakeTransport {
        const transport: FakeTransport = new FakeTransport();

        transport.onSend = (message: Uint8Array): void => {
            const payload: number[] = message[2] === Command.GetInfo ? deviceInfoPayload(packetDataMax, 0) : [];

            answerWith(transport, Status.Success, payload)(message);
        };

        return transport;
    }

    it("refuses a zero chunk size before erase is sent", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = infoOnlyTransport(0);
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        await client.open();
        await assert.rejects(
            flash(client, plainFlash, { start: false, reset: false, chunkSize: 0 }, new Log("quiet")),
            VbootError
        );
        assert.equal(transport.calls.some((packet: Uint8Array): boolean => packet[2] === Command.Erase), false);
    });

    it("refuses a chunk size above the device packet size before erase is sent", asyncTestOptions, async (): Promise<void> => {
        const transport: FakeTransport = infoOnlyTransport(0);
        const client: BootloaderClient = new BootloaderClient(transport, clientOptions({}));

        await client.open();
        await assert.rejects(
            flash(client, plainFlash, { start: false, reset: false, chunkSize: 256 }, new Log("quiet")),
            VbootError
        );
        assert.equal(transport.calls.some((packet: Uint8Array): boolean => packet[2] === Command.Erase), false);
    });
});
