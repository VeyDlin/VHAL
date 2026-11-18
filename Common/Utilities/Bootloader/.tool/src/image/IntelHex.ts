import { concatBytes } from "../util/Bytes.ts";
import { VbootError } from "../util/Errors.ts";
import { fromHex, toHex } from "../util/Hex.ts";
import type { LoadSegment } from "./Elf.ts";


interface IntelRecord {
    address: number;

    type: number;

    data: Uint8Array;
}


interface PendingSegment {
    address: number;

    end: number;

    chunks: Uint8Array[];
}

const maxRecordData: number = 16;
const linearBlockSize: number = 0x1_0000;
const addressSpaceSize: number = 0x1_0000_0000;
const recordData: number = 0x00;
const recordEndOfFile: number = 0x01;
const recordSegmentAddress: number = 0x02;
const recordStartSegmentAddress: number = 0x03;
const recordLinearAddress: number = 0x04;
const recordStartLinearAddress: number = 0x05;
const hexDigitsPattern: RegExp = /^[0-9A-Fa-f]*$/;


// Records never cross a 64 KB boundary: an extended linear address record is emitted whenever the upper 16 bits change,
// and a data record is cut at the next boundary, so a base address that is not aligned still produces valid records.
export function writeIntelHex(base: number, data: Uint8Array): string {
    if (!Number.isInteger(base) || base < 0 || base + data.length > addressSpaceSize) {
        throw new VbootError(`the image does not fit the 32-bit address space at base 0x${base.toString(16)}`);
    }

    const lines: string[] = [];
    let linearBase: number = -1;
    let offset: number = 0;
    while (offset < data.length) {
        const address: number = base + offset;
        const upper: number = Math.floor(address / linearBlockSize);
        if (upper !== linearBase) {
            linearBase = upper;
            lines.push(formatRecord(0, recordLinearAddress, Uint8Array.of(upper >> 8, upper & 0xff)));
        }
        const low: number = address % linearBlockSize;
        const count: number = Math.min(maxRecordData, data.length - offset, linearBlockSize - low);
        lines.push(formatRecord(low, recordData, data.subarray(offset, offset + count)));
        offset += count;
    }
    lines.push(formatRecord(0, recordEndOfFile, new Uint8Array(0)));
    return lines.join("\n") + "\n";
}


export function readIntelHex(text: string): LoadSegment[] {
    const records: LoadSegment[] = [];
    let baseAddress: number = 0;
    let sawEndOfFile: boolean = false;
    let lineNumber: number = 0;
    for (const rawLine of text.split(/\r?\n/)) {
        lineNumber++;
        const line: string = rawLine.trim();
        if (line === "") {
            continue;
        }
        if (sawEndOfFile) {
            throw new VbootError(`Intel HEX line ${lineNumber}: data after the end-of-file record`);
        }

        const record: IntelRecord = parseRecord(line, lineNumber);
        if (record.type === recordData) {
            if (record.data.length > 0) {
                records.push({ address: baseAddress + record.address, data: record.data });
            }
        } else if (record.type === recordEndOfFile) {
            sawEndOfFile = true;
        } else if (record.type === recordSegmentAddress) {
            baseAddress = readWord(record, lineNumber) * 16;
        } else if (record.type === recordLinearAddress) {
            baseAddress = readWord(record, lineNumber) * linearBlockSize;
        } else if (record.type !== recordStartSegmentAddress && record.type !== recordStartLinearAddress) {
            throw new VbootError(`Intel HEX line ${lineNumber}: unknown record type 0x${record.type.toString(16)}`);
        }
    }

    if (!sawEndOfFile) {
        throw new VbootError("Intel HEX data has no end-of-file record");
    }

    records.sort((a: LoadSegment, b: LoadSegment): number => a.address - b.address);
    const segments: LoadSegment[] = [];
    let pending: PendingSegment | null = null;
    for (const record of records) {
        if (pending !== null && record.address < pending.end) {
            throw new VbootError(`Intel HEX data overlaps at 0x${record.address.toString(16)}`);
        }
        if (pending !== null && record.address === pending.end) {
            pending.chunks.push(record.data);
            pending.end += record.data.length;
        } else {
            if (pending !== null) {
                segments.push(finishSegment(pending));
            }
            pending = {
                address: record.address,
                end: record.address + record.data.length,
                chunks: [record.data]
            };
        }
    }
    if (pending !== null) {
        segments.push(finishSegment(pending));
    }
    return segments;
}


function formatRecord(address: number, type: number, data: Uint8Array): string {
    const bytes: Uint8Array = new Uint8Array(4 + data.length + 1);
    bytes[0] = data.length;
    bytes[1] = (address >> 8) & 0xff;
    bytes[2] = address & 0xff;
    bytes[3] = type;
    bytes.set(data, 4);

    let sum: number = 0;
    for (const byte of bytes.subarray(0, bytes.length - 1)) {
        sum += byte;
    }
    bytes[bytes.length - 1] = (-sum) & 0xff;
    return (":" + toHex(bytes)).toUpperCase();
}


function parseRecord(line: string, lineNumber: number): IntelRecord {
    if (!line.startsWith(":")) {
        throw new VbootError(`Intel HEX line ${lineNumber}: a record must start with ':'`);
    }
    const body: string = line.slice(1);
    if (body.length % 2 !== 0 || !hexDigitsPattern.test(body)) {
        throw new VbootError(`Intel HEX line ${lineNumber}: the record is not valid hexadecimal`);
    }

    const bytes: Uint8Array = fromHex(body);
    if (bytes.length < 5) {
        throw new VbootError(`Intel HEX line ${lineNumber}: the record is too short`);
    }
    const view: DataView = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const count: number = view.getUint8(0);
    if (bytes.length !== count + 5) {
        throw new VbootError(`Intel HEX line ${lineNumber}: the byte count does not match the record length`);
    }

    let sum: number = 0;
    for (const byte of bytes) {
        sum += byte;
    }
    if ((sum & 0xff) !== 0) {
        throw new VbootError(`Intel HEX line ${lineNumber}: checksum mismatch`);
    }

    return {
        address: view.getUint16(1, false),
        type: view.getUint8(3),
        data: bytes.subarray(4, 4 + count)
    };
}


function readWord(record: IntelRecord, lineNumber: number): number {
    if (record.data.length !== 2) {
        throw new VbootError(`Intel HEX line ${lineNumber}: an address record must carry 2 bytes`);
    }
    return new DataView(record.data.buffer, record.data.byteOffset, record.data.byteLength).getUint16(0, false);
}


function finishSegment(pending: PendingSegment): LoadSegment {
    return { address: pending.address, data: concatBytes(pending.chunks) };
}
