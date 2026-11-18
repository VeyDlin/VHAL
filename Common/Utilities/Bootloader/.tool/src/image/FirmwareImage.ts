import { readFile } from "node:fs/promises";
import { extname } from "node:path";
import { VbootError } from "../util/Errors.ts";
import { readElfLoadSegments } from "./Elf.ts";
import type { LoadSegment } from "./Elf.ts";
import { readIntelHex } from "./IntelHex.ts";


export interface FirmwareImage {
    base: number | null;

    data: Uint8Array;
}

// Largest address span an image may cover. Anything bigger is a malformed file, not a firmware image,
// and would otherwise be allocated in full before any region check runs.
const maxImageSpan: number = 64 * 1024 * 1024;
const elfMagic: Uint8Array = new Uint8Array([0x7f, 0x45, 0x4c, 0x46]);
const minIntelHexLineDigits: number = 10;


// The format is taken from the content, not the file name: a .axf or .out ELF must not be read as raw bytes.
// base is the load address when the file carries one (ELF, HEX); a raw binary has none unless baseOverride is given.
export async function loadImage(
    path: string,
    baseOverride: number | null): Promise<FirmwareImage>
{
    const bytes: Uint8Array = new Uint8Array(await readFile(path));
    const extension: string = extname(path).toLowerCase();
    const isElf: boolean = startsWithBytes(bytes, elfMagic);
    const isHex: boolean = looksLikeIntelHex(bytes);

    if (extension === ".elf" && !isElf) {
        throw new VbootError(`${path} is not an ELF file`);
    }
    if (extension === ".hex" && !isHex) {
        throw new VbootError(`${path} is not an Intel HEX file`);
    }

    if (isElf || isHex) {
        const segments: LoadSegment[] = isElf
            ? readElfLoadSegments(bytes)
            : readIntelHex(new TextDecoder().decode(bytes));
        const image: FirmwareImage = flattenSegments(segments);
        if (baseOverride !== null && baseOverride !== image.base) {
            throw new VbootError(
                `the base 0x${baseOverride.toString(16)} differs from the load address 0x${(image.base ?? 0).toString(16)}`);
        }
        return image;
    }

    if (bytes.length === 0) {
        throw new VbootError(`${path} is empty`);
    }
    return { base: baseOverride, data: bytes };
}


// Gaps between segments are filled with 0xFF, the erased state of flash.
export function flattenSegments(segments: LoadSegment[]): FirmwareImage {
    if (segments.length === 0) {
        throw new VbootError("the image has no loadable data");
    }

    let start: number = Number.MAX_SAFE_INTEGER;
    let end: number = 0;
    for (const segment of segments) {
        start = Math.min(start, segment.address);
        end = Math.max(end, segment.address + segment.data.length);
    }

    // Checked before the allocation below, which is as large as the span.
    if (end - start > maxImageSpan) {
        throw new VbootError(
            `the image spans 0x${start.toString(16)}..0x${end.toString(16)}, more than ${maxImageSpan} bytes`);
    }

    const data: Uint8Array = new Uint8Array(end - start).fill(0xff);
    for (const segment of segments) {
        data.set(segment.data, segment.address - start);
    }
    return { base: start, data };
}


function startsWithBytes(bytes: Uint8Array, prefix: Uint8Array): boolean {
    if (bytes.length < prefix.length) {
        return false;
    }
    return prefix.every((value: number, index: number): boolean => bytes[index] === value);
}


// An Intel HEX file starts with a record: ':' followed by at least a whole record header of hex digits, up to the
// end of the line. Checking the first line keeps a raw binary whose first byte is ':' from being read as text.
function looksLikeIntelHex(bytes: Uint8Array): boolean {
    let index: number = 0;
    while (index < bytes.length && isAsciiWhitespace(bytes[index] ?? 0)) {
        index++;
    }
    if (bytes[index] !== 0x3a) {
        return false;
    }

    index++;
    const digitsStart: number = index;
    while (index < bytes.length && isHexDigit(bytes[index] ?? 0)) {
        index++;
    }
    const digits: number = index - digitsStart;
    const lineEnds: boolean = index === bytes.length || isAsciiWhitespace(bytes[index] ?? 0);
    return digits >= minIntelHexLineDigits && lineEnds;
}


function isAsciiWhitespace(value: number): boolean {
    return value === 0x20 || value === 0x09 || value === 0x0a || value === 0x0d;
}


function isHexDigit(value: number): boolean {
    return (value >= 0x30 && value <= 0x39) || (value >= 0x41 && value <= 0x46) || (value >= 0x61 && value <= 0x66);
}
