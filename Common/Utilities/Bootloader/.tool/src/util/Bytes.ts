import { VbootError } from "./Errors.ts";


export function concatBytes(parts: Uint8Array[]): Uint8Array {
    let total: number = 0;

    for (const part of parts) {
        total += part.length;
    }

    const result: Uint8Array = new Uint8Array(total);
    let position: number = 0;

    for (const part of parts) {
        result.set(part, position);
        position += part.length;
    }

    return result;
}


export function readU32(data: Uint8Array, offset: number): number {
    checkRange(data, offset, 4);
    return new DataView(data.buffer, data.byteOffset, data.byteLength).getUint32(offset, true);
}


export function readU64(data: Uint8Array, offset: number): bigint {
    checkRange(data, offset, 8);
    return new DataView(data.buffer, data.byteOffset, data.byteLength).getBigUint64(offset, true);
}


export function writeU32(target: Uint8Array, offset: number, value: number): void {
    if (!Number.isInteger(value) || value < 0 || value > 0xFFFFFFFF) {
        throw new VbootError(`value ${value} does not fit in an unsigned 32-bit field`);
    }

    checkRange(target, offset, 4);
    new DataView(target.buffer, target.byteOffset, target.byteLength).setUint32(offset, value, true);
}


export function u32Bytes(value: number): Uint8Array {
    const bytes: Uint8Array = new Uint8Array(4);
    writeU32(bytes, 0, value);
    return bytes;
}


export function equalBytes(a: Uint8Array, b: Uint8Array): boolean {
    if (a.length !== b.length) {
        return false;
    }

    for (let index: number = 0; index < a.length; index++) {
        if (a[index] !== b[index]) {
            return false;
        }
    }

    return true;
}


function checkRange(data: Uint8Array, offset: number, size: number): void {
    if (!Number.isInteger(offset) || offset < 0 || offset + size > data.length) {
        throw new VbootError(`read or write of ${size} bytes at offset ${offset} is outside a ${data.length}-byte buffer`);
    }
}
