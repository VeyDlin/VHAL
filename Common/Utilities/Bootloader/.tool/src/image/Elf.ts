import { VbootError } from "../util/Errors.ts";


export interface LoadSegment {
    address: number;

    data: Uint8Array;
}

const elfHeaderSize: number = 52;
const programHeaderMinSize: number = 32;
const elfClass32: number = 1;
const elfDataLittleEndian: number = 1;
const programTypeLoad: number = 1;
const offsetProgramHeaderOffset: number = 0x1c;
const offsetProgramHeaderEntrySize: number = 0x2a;
const offsetProgramHeaderCount: number = 0x2c;


// Reads the PT_LOAD segments of an ELF32 little-endian file. The load address is the physical address (LMA),
// which is where the bytes are stored in flash. Bytes between p_filesz and p_memsz are not part of the image.
export function readElfLoadSegments(file: Uint8Array): LoadSegment[] {
    if (file.length < elfHeaderSize) {
        throw new VbootError("the file is too short to be an ELF file");
    }
    if (file[0] !== 0x7f || file[1] !== 0x45 || file[2] !== 0x4c || file[3] !== 0x46) {
        throw new VbootError("not an ELF file");
    }
    if (file[4] !== elfClass32) {
        throw new VbootError("only ELF32 files are supported");
    }
    if (file[5] !== elfDataLittleEndian) {
        throw new VbootError("only little-endian ELF files are supported");
    }

    const view: DataView = new DataView(file.buffer, file.byteOffset, file.byteLength);
    const programHeaderOffset: number = view.getUint32(offsetProgramHeaderOffset, true);
    const programHeaderEntrySize: number = view.getUint16(offsetProgramHeaderEntrySize, true);
    const programHeaderCount: number = view.getUint16(offsetProgramHeaderCount, true);
    if (programHeaderCount === 0) {
        return [];
    }
    if (programHeaderEntrySize < programHeaderMinSize) {
        throw new VbootError(`ELF program header entry size ${programHeaderEntrySize} is too small`);
    }
    if (programHeaderOffset + programHeaderCount * programHeaderEntrySize > file.length) {
        throw new VbootError("the ELF program header table lies outside the file");
    }

    const segments: LoadSegment[] = [];
    for (let index: number = 0; index < programHeaderCount; index++) {
        const entry: number = programHeaderOffset + index * programHeaderEntrySize;
        if (view.getUint32(entry, true) !== programTypeLoad) {
            continue;
        }
        const fileSize: number = view.getUint32(entry + 16, true);
        if (fileSize === 0) {
            continue;
        }
        const fileOffset: number = view.getUint32(entry + 4, true);
        if (fileOffset + fileSize > file.length) {
            throw new VbootError(`ELF load segment ${index} lies outside the file`);
        }
        segments.push({
            address: view.getUint32(entry + 12, true),
            data: file.slice(fileOffset, fileOffset + fileSize)
        });
    }

    segments.sort((a: LoadSegment, b: LoadSegment): number => a.address - b.address);
    let previous: LoadSegment | null = null;
    for (const segment of segments) {
        if (previous !== null && previous.address + previous.data.length > segment.address) {
            throw new VbootError(`ELF load segments overlap at 0x${segment.address.toString(16)}`);
        }
        previous = segment;
    }
    return segments;
}
