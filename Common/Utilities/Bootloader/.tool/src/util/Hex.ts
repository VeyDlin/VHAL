import { VbootError } from "./Errors.ts";


export function toHex(data: Uint8Array): string {
    const parts: string[] = [];

    for (const byte of data) {
        parts.push(byte.toString(16).padStart(2, "0"));
    }

    return parts.join("");
}


export function fromHex(text: string): Uint8Array {
    let digits: string = text.trim().replace(/[\s:,]/g, "");

    if (/^0x/i.test(digits)) {
        digits = digits.slice(2);
    }

    if (!/^[0-9a-fA-F]*$/.test(digits)) {
        throw new VbootError(`invalid hex text "${text}": only hex digits, spaces, ":" and "," are allowed`);
    }

    if (digits.length % 2 !== 0) {
        throw new VbootError(`invalid hex text "${text}": odd number of hex digits`);
    }

    const bytes: Uint8Array = new Uint8Array(digits.length / 2);

    for (let index: number = 0; index < bytes.length; index++) {
        bytes[index] = parseInt(digits.substring(index * 2, index * 2 + 2), 16);
    }

    return bytes;
}


export function parseInteger(text: string): number {
    const trimmed: string = text.trim();
    let value: number;

    // A leading zero suggests a hex address written without 0x, which would otherwise be read as decimal
    if (/^0[0-9]+$/.test(trimmed)) {
        throw new VbootError(`ambiguous number "${text}": use 0x for hexadecimal`);
    }

    if (/^[0-9]+$/.test(trimmed)) {
        value = Number(trimmed);
    } else if (/^0x[0-9a-fA-F]+$/i.test(trimmed)) {
        value = parseInt(trimmed.slice(2), 16);
    } else {
        throw new VbootError(`invalid number "${text}": use decimal or 0x-prefixed hexadecimal`);
    }

    if (!Number.isSafeInteger(value)) {
        throw new VbootError(`number "${text}" is out of range`);
    }

    return value;
}
