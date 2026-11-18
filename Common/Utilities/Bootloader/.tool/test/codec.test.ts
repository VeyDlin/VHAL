import assert from "node:assert/strict";
import { describe, it } from "node:test";

import {
    Command,
    commandHeader,
    commandName,
    defaultPacketDataMaxSize,
    encodeCommand,
    encodeFirmwareHeader,
    parseDeviceInfo,
    parseDeviceStatus,
    ResponseParser,
    responseHeader,
    Status,
    statusName
} from "../src/protocol/Bootloader.ts";
import type { DeviceInfo, DeviceStatus, Response } from "../src/protocol/Bootloader.ts";
import { VbootError } from "../src/util/Errors.ts";


interface DeviceInfoFields {
    version: bigint;
    maxPacket: number;
    start: number;
    size: number;
    protection: number;
    denied: number;
    packetMax: number;
    cipher: number;
}


function responseBytes(sequence: number, status: number, data: number[]): Uint8Array {
    return Uint8Array.from([responseHeader, sequence, status, data.length, ...data]);
}


function deviceInfoBytes(fields: DeviceInfoFields): Uint8Array {
    const data: Uint8Array = new Uint8Array(24);
    const view: DataView = new DataView(data.buffer);

    view.setBigUint64(0, fields.version, true);
    view.setUint32(8, fields.maxPacket, true);
    view.setUint32(12, fields.start, true);
    view.setUint32(16, fields.size, true);
    data[20] = fields.protection;
    data[21] = fields.denied;
    data[22] = fields.packetMax;
    data[23] = fields.cipher;

    return data;
}


function expectResponse(actual: Response | undefined, sequence: number, status: number, data: number[]): void {
    assert.ok(actual !== undefined, "no response was parsed");
    assert.equal(actual.sequence, sequence);
    assert.equal(actual.status, status);
    assert.deepEqual(Array.from(actual.data), data);
}


describe("wire constants", (): void => {
    it("match IBootloader.h", (): void => {
        assert.equal(commandHeader, 0xAA);
        assert.equal(responseHeader, 0xBB);
        assert.equal(defaultPacketDataMaxSize, 240);
        assert.equal(Command.Ping, 0x01);
        assert.equal(Command.GetInfo, 0x02);
        assert.equal(Command.Erase, 0x10);
        assert.equal(Command.Write, 0x11);
        assert.equal(Command.Read, 0x12);
        assert.equal(Command.Verify, 0x13);
        assert.equal(Command.Finalize, 0x14);
        assert.equal(Command.ResetWritePos, 0x15);
        assert.equal(Command.ResetReadPos, 0x16);
        assert.equal(Command.GetHash, 0x17);
        assert.equal(Command.SetIV, 0x18);
        assert.equal(Command.Start, 0x20);
        assert.equal(Command.Reset, 0x21);
        assert.equal(Command.GetStatus, 0x30);
        assert.equal(Command.UnlockDevice, 0x40);
        assert.equal(Command.UserData, 0x50);
        assert.equal(Status.Success, 0x00);
        assert.equal(Status.Error, 0x01);
        assert.equal(Status.InvalidCommand, 0x02);
        assert.equal(Status.InvalidParameters, 0x03);
        assert.equal(Status.Busy, 0x04);
        assert.equal(Status.NotReady, 0x05);
        assert.equal(Status.Protected, 0x06);
        assert.equal(Status.CryptoError, 0x07);
        assert.equal(Status.AccessDenied, 0x08);
        assert.equal(Status.EndOfData, 0x09);
        assert.equal(Status.ChecksumMismatch, 0x0A);
    });

    it("give readable names for commands and statuses", (): void => {
        assert.match(commandName(Command.Finalize), /finalize/i);
        assert.notEqual(statusName(Status.ChecksumMismatch), statusName(Status.Success));
        assert.ok(statusName(0xEE).length > 0);
    });
});


describe("encodeCommand", (): void => {
    it("writes header, sequence, command, length and data", (): void => {
        const packet: Uint8Array = encodeCommand(7, Command.Write, Uint8Array.from([1, 2, 3]));

        assert.deepEqual(Array.from(packet), [0xAA, 7, 0x11, 3, 1, 2, 3]);
    });

    it("encodes a command without data with length zero", (): void => {
        assert.deepEqual(Array.from(encodeCommand(255, Command.Ping, new Uint8Array(0))), [0xAA, 0xFF, 0x01, 0]);
    });
});


describe("ResponseParser", (): void => {
    it("parses a response that arrives in one chunk", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const responses: Response[] = parser.push(responseBytes(9, Status.Success, [0xAA, 0xBB]));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 9, Status.Success, [0xAA, 0xBB]);
    });

    it("parses a response without data", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const responses: Response[] = parser.push(responseBytes(3, Status.Busy, []));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 3, Status.Busy, []);
    });

    it("assembles a response split over several chunks", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const frame: Uint8Array = responseBytes(4, Status.Success, [10, 20, 30, 40]);

        assert.deepEqual(parser.push(frame.subarray(0, 2)), []);
        assert.deepEqual(parser.push(frame.subarray(2, 5)), []);

        const responses: Response[] = parser.push(frame.subarray(5));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 4, Status.Success, [10, 20, 30, 40]);
    });

    it("yields two responses that arrive in one chunk, in order", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const first: Uint8Array = responseBytes(1, Status.Success, [0x01]);
        const second: Uint8Array = responseBytes(2, Status.EndOfData, []);
        const responses: Response[] = parser.push(Uint8Array.from([...first, ...second]));

        assert.equal(responses.length, 2);
        expectResponse(responses[0], 1, Status.Success, [0x01]);
        expectResponse(responses[1], 2, Status.EndOfData, []);
    });

    it("skips garbage before the response header", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const noise: number[] = [0x00, 0x13, 0x7F, 0xAA, 0x01];
        const responses: Response[] = parser.push(Uint8Array.from([...noise, ...responseBytes(5, Status.Success, [0x42])]));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 5, Status.Success, [0x42]);
    });

    it("skips a stray 0xBB whose status is unknown", (): void => {
        const parser: ResponseParser = new ResponseParser();
        const stray: number[] = [responseHeader, 1, 0x7F, 5];
        const responses: Response[] = parser.push(Uint8Array.from([...stray, ...responseBytes(5, Status.Success, [0x42])]));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 5, Status.Success, [0x42]);
    });

    it("drops a partial response on clear()", (): void => {
        const parser: ResponseParser = new ResponseParser();

        assert.deepEqual(parser.push(Uint8Array.from([responseHeader, 1, 0])), []);
        parser.clear();

        const responses: Response[] = parser.push(responseBytes(2, Status.Success, [0x99]));

        assert.equal(responses.length, 1);
        expectResponse(responses[0], 2, Status.Success, [0x99]);
    });
});


describe("parseDeviceInfo", (): void => {
    it("reads all 24 bytes of GetInfo", (): void => {
        const info: DeviceInfo = parseDeviceInfo(deviceInfoBytes({
            version: 0x00010000n,
            maxPacket: 20,
            start: 0x08020000,
            size: 0x40000,
            protection: 1,
            denied: 1,
            packetMax: 240,
            cipher: 0
        }));

        assert.equal(info.bootloaderVersion, 0x00010000n);
        assert.equal(info.maxPacketSize, 20);
        assert.equal(info.memoryStart, 0x08020000);
        assert.equal(info.memorySize, 0x40000);
        assert.equal(info.protectionLevel, 1);
        assert.equal(info.accessDenied, true);
        assert.equal(info.packetDataMaxSize, 240);
        assert.equal(info.cipher, "none-or-unknown");
    });

    it("treats a packetDataMaxSize of zero as 240", (): void => {
        const info: DeviceInfo = parseDeviceInfo(deviceInfoBytes({
            version: 1n,
            maxPacket: 20,
            start: 0,
            size: 0,
            protection: 0,
            denied: 0,
            packetMax: 0,
            cipher: 0
        }));

        assert.equal(info.packetDataMaxSize, 240);
        assert.equal(info.accessDenied, false);
    });

    it("maps the cipher byte to its name", (): void => {
        const base: DeviceInfoFields = {
            version: 1n,
            maxPacket: 20,
            start: 0,
            size: 0,
            protection: 0,
            denied: 0,
            packetMax: 240,
            cipher: 0
        };
        const names: Map<number, string> = new Map<number, string>([
            [0, "none-or-unknown"],
            [1, "aes-ecb"],
            [2, "aes-cbc"],
            [3, "aes-ctr"]
        ]);

        for (const [code, name] of names) {
            const info: DeviceInfo = parseDeviceInfo(deviceInfoBytes({ ...base, cipher: code }));

            assert.equal(info.cipher, name);
        }
    });

    it("rejects a payload shorter than 24 bytes", (): void => {
        assert.throws((): DeviceInfo => parseDeviceInfo(new Uint8Array(23)), VbootError);
    });
});


describe("parseDeviceStatus", (): void => {
    it("reads state and bytesReceived", (): void => {
        const data: Uint8Array = new Uint8Array(8);

        data[0] = 2;
        new DataView(data.buffer).setUint32(4, 0x00001234, true);

        const status: DeviceStatus = parseDeviceStatus(data);

        assert.equal(status.state, "processing");
        assert.equal(status.bytesReceived, 0x1234);
    });

    it("names the known states", (): void => {
        const expected: Array<[number, string]> = [
            [0, "idle"],
            [1, "receiving"],
            [2, "processing"],
            [3, "error"]
        ];

        for (const [code, name] of expected) {
            const data: Uint8Array = new Uint8Array(8);

            data[0] = code;
            assert.equal(parseDeviceStatus(data).state, name);
        }
    });

    it("keeps unknown states as numbers", (): void => {
        const data: Uint8Array = new Uint8Array(8);

        data[0] = 7;
        assert.equal(parseDeviceStatus(data).state, 7);
    });
});


describe("encodeFirmwareHeader", (): void => {
    it("writes crc32 and size as little-endian u32 values", (): void => {
        assert.deepEqual(
            Array.from(encodeFirmwareHeader(0xCBF43926, 0x00001234)),
            [0x26, 0x39, 0xF4, 0xCB, 0x34, 0x12, 0x00, 0x00]
        );
    });
});
