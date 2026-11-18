import { VbootError } from "../util/Errors.ts";
import { concatBytes, readU32, readU64, writeU32 } from "../util/Bytes.ts";


// Mirrors IBootloader.h (Command, BootloaderStatus, packet layouts, DeviceInfo, GetStatus, FirmwareHeader)

export const Command = {
    Ping: 0x01,
    GetInfo: 0x02,
    Erase: 0x10,
    Write: 0x11,
    Read: 0x12,
    Verify: 0x13,
    Finalize: 0x14,
    ResetWritePos: 0x15,
    ResetReadPos: 0x16,
    GetHash: 0x17,
    SetIV: 0x18,
    Start: 0x20,
    Reset: 0x21,
    GetStatus: 0x30,
    UnlockDevice: 0x40,
    UserData: 0x50
} as const;


export const Status = {
    Success: 0x00,
    Error: 0x01,
    InvalidCommand: 0x02,
    InvalidParameters: 0x03,
    Busy: 0x04,
    NotReady: 0x05,
    Protected: 0x06,
    CryptoError: 0x07,
    AccessDenied: 0x08,
    EndOfData: 0x09,
    ChecksumMismatch: 0x0A
} as const;


export type CipherId = "none-or-unknown" | "aes-ecb" | "aes-cbc" | "aes-ctr";


export const commandHeader: number = 0xAA;


export const responseHeader: number = 0xBB;


export const defaultPacketDataMaxSize: number = 240;


const deviceInfoSize: number = 24;


const deviceStatusSize: number = 8;


const firmwareHeaderSize: number = 8;


const commandNames: Map<number, string> = buildNameMap(Command);


const statusNames: Map<number, string> = buildNameMap(Status);


const knownStatusCodes: Set<number> = new Set<number>(Object.values(Status));


// GetInfo cipher byte: 0 = none/unknown, 1 = AES-ECB, 2 = AES-CBC, 3 = AES-CTR (the key size is not reported)
const cipherIds: readonly CipherId[] = ["none-or-unknown", "aes-ecb", "aes-cbc", "aes-ctr"];


export function commandName(command: number): string {
    return commandNames.get(command) ?? `Unknown(0x${hexByte(command)})`;
}


export function statusName(status: number): string {
    return statusNames.get(status) ?? `Unknown(0x${hexByte(status)})`;
}


export interface Response {
    sequence: number;

    status: number;

    data: Uint8Array;
}


export interface DeviceInfo {
    bootloaderVersion: bigint;

    maxPacketSize: number;

    memoryStart: number;

    memorySize: number;

    protectionLevel: number;

    accessDenied: boolean;

    packetDataMaxSize: number;

    cipher: CipherId;
}


export interface DeviceStatus {
    state: "idle" | "receiving" | "processing" | "error" | number;

    bytesReceived: number;
}


// Command packet: header 0xAA, sequence, command, data length, data
export function encodeCommand(sequence: number, command: number, data: Uint8Array): Uint8Array {
    if (!Number.isInteger(sequence) || sequence < 0 || sequence > 0xFF) {
        throw new VbootError(`command sequence ${sequence} is not a byte`);
    }

    if (!Number.isInteger(command) || command < 0 || command > 0xFF) {
        throw new VbootError(`command code ${command} is not a byte`);
    }

    if (data.length > 0xFF) {
        throw new VbootError(`command data is ${data.length} bytes, the packet length field allows 255`);
    }

    const packet: Uint8Array = new Uint8Array(4 + data.length);
    packet[0] = commandHeader;
    packet[1] = sequence;
    packet[2] = command;
    packet[3] = data.length;
    packet.set(data, 4);
    return packet;
}


// Collects the bytes of response packets (header 0xBB, sequence, status, length, data) from a byte stream.
// Bytes before a header are skipped; a header whose status is not a known code is skipped as well, so a stray 0xBB
// in the stream does not swallow the response that follows it.
export class ResponseParser {
    private pending: Uint8Array = new Uint8Array(0);


    public push(chunk: Uint8Array): Response[] {
        this.pending = concatBytes([this.pending, chunk]);
        const responses: Response[] = [];

        for (;;) {
            const start: number = this.pending.indexOf(responseHeader);

            if (start < 0) {
                this.pending = new Uint8Array(0);
                break;
            }

            if (start > 0) {
                this.pending = this.pending.slice(start);
            }

            if (this.pending.length < 4) {
                break;
            }

            const status: number = this.pending[2] as number;

            if (!knownStatusCodes.has(status)) {
                this.pending = this.pending.slice(1);
                continue;
            }

            const length: number = this.pending[3] as number;

            if (this.pending.length < 4 + length) {
                break;
            }

            responses.push({
                sequence: this.pending[1] as number,
                status: status,
                data: this.pending.slice(4, 4 + length)
            });
            this.pending = this.pending.slice(4 + length);
        }

        return responses;
    }


    public clear(): void {
        this.pending = new Uint8Array(0);
    }
}


// GetInfo payload, 24 bytes: u64 bootloaderVersion, u32 maxPacketSize, u32 memoryStart, u32 memorySize,
// u8 protectionLevel, u8 accessDenied, u8 packetDataMaxSize (0 = default), u8 cipher
export function parseDeviceInfo(data: Uint8Array): DeviceInfo {
    if (data.length < deviceInfoSize) {
        throw new VbootError(`GetInfo answer is ${data.length} bytes, expected ${deviceInfoSize}`);
    }

    const rawPacketDataMaxSize: number = byteAt(data, 22);
    const cipherId: number = byteAt(data, 23);
    const cipher: CipherId | undefined = cipherIds[cipherId];

    if (cipher === undefined) {
        throw new VbootError(`GetInfo reports unknown cipher id ${cipherId}`);
    }

    return {
        bootloaderVersion: readU64(data, 0),
        maxPacketSize: readU32(data, 8),
        memoryStart: readU32(data, 12),
        memorySize: readU32(data, 16),
        protectionLevel: byteAt(data, 20),
        accessDenied: byteAt(data, 21) !== 0,
        packetDataMaxSize: rawPacketDataMaxSize === 0 ? defaultPacketDataMaxSize : rawPacketDataMaxSize,
        cipher: cipher
    };
}


// GetStatus payload, 8 bytes: u8 state, 3 reserved bytes, u32 bytesReceived
export function parseDeviceStatus(data: Uint8Array): DeviceStatus {
    if (data.length < deviceStatusSize) {
        throw new VbootError(`GetStatus answer is ${data.length} bytes, expected ${deviceStatusSize}`);
    }

    const state: number = byteAt(data, 0);
    const stateName: DeviceStatus["state"] = stateNameOf(state);

    return {
        state: stateName,
        bytesReceived: readU32(data, 4)
    };
}


// Expected FirmwareHeader sent with Finalize: u32 crc32, u32 size, little-endian
export function encodeFirmwareHeader(crc: number, size: number): Uint8Array {
    const header: Uint8Array = new Uint8Array(firmwareHeaderSize);
    writeU32(header, 0, crc);
    writeU32(header, 4, size);
    return header;
}


function buildNameMap(table: Record<string, number>): Map<number, string> {
    const names: Map<number, string> = new Map<number, string>();

    for (const [name, code] of Object.entries(table)) {
        names.set(code, name);
    }

    return names;
}


function stateNameOf(state: number): DeviceStatus["state"] {
    switch (state) {
        case 0:
            return "idle";
        case 1:
            return "receiving";
        case 2:
            return "processing";
        case 3:
            return "error";
        default:
            return state;
    }
}


function byteAt(data: Uint8Array, offset: number): number {
    return data[offset] as number;
}


function hexByte(value: number): string {
    return value.toString(16).padStart(2, "0");
}
