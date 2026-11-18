import assert from "node:assert/strict";
import { mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { readElfLoadSegments } from "../src/image/Elf.ts";
import type { LoadSegment } from "../src/image/Elf.ts";
import { readIntelHex, writeIntelHex } from "../src/image/IntelHex.ts";
import { flattenSegments, loadImage } from "../src/image/FirmwareImage.ts";
import type { FirmwareImage } from "../src/image/FirmwareImage.ts";
import { mergeFullImage } from "../src/image/Merge.ts";
import { VbootError } from "../src/util/Errors.ts";
import { crc32 } from "../src/util/Crc32.ts";
import { readU32 } from "../src/util/Bytes.ts";


const regionStart: number = 0x08020000;
const regionSize: number = 0x40000;
const ptLoad: number = 1;
const ptNote: number = 4;
// A pending promise must fail its test instead of hanging the whole run
const asyncTestOptions: { timeout: number } = { timeout: 10000 };


interface TestSegment {
    type: number;
    vaddr: number;
    paddr: number;
    fileData: Uint8Array;
    memsz: number;
}


function patternBytes(count: number, seed: number): Uint8Array {
    const result: Uint8Array = new Uint8Array(count);

    for (let i: number = 0; i < count; i++) {
        result[i] = (seed + i * 13) & 0xFF;
    }

    return result;
}


function buildElf32(segments: TestSegment[]): Uint8Array {
    const headerSize: number = 52;
    const programHeaderSize: number = 32;
    const dataStart: number = headerSize + programHeaderSize * segments.length;
    const total: number = segments.reduce(
        (sum: number, segment: TestSegment): number => sum + segment.fileData.length,
        dataStart
    );
    const file: Uint8Array = new Uint8Array(total);
    const view: DataView = new DataView(file.buffer);
    let offset: number = dataStart;

    file.set([0x7F, 0x45, 0x4C, 0x46, 1, 1, 1], 0);
    view.setUint16(16, 2, true);
    view.setUint16(18, 40, true);
    view.setUint32(20, 1, true);
    view.setUint32(28, headerSize, true);
    view.setUint16(40, headerSize, true);
    view.setUint16(42, programHeaderSize, true);
    view.setUint16(44, segments.length, true);
    view.setUint16(46, 40, true);

    for (let i: number = 0; i < segments.length; i++) {
        const segment: TestSegment | undefined = segments[i];

        if (segment === undefined) {
            continue;
        }

        const base: number = headerSize + i * programHeaderSize;

        view.setUint32(base, segment.type, true);
        view.setUint32(base + 4, offset, true);
        view.setUint32(base + 8, segment.vaddr, true);
        view.setUint32(base + 12, segment.paddr, true);
        view.setUint32(base + 16, segment.fileData.length, true);
        view.setUint32(base + 20, segment.memsz, true);
        view.setUint32(base + 24, 5, true);
        view.setUint32(base + 28, 4, true);
        file.set(segment.fileData, offset);
        offset += segment.fileData.length;
    }

    return file;
}


function checksumValid(line: string): boolean {
    let sum: number = 0;

    for (let i: number = 1; i + 1 < line.length; i += 2) {
        sum += parseInt(line.substring(i, i + 2), 16);
    }

    return (sum & 0xFF) === 0;
}


describe("ELF reader", (): void => {
    it("returns PT_LOAD segments at their load (physical) addresses, sorted by address", (): void => {
        const file: Uint8Array = buildElf32([
            {
                type: ptLoad,
                vaddr: 0x20000100,
                paddr: 0x08000100,
                fileData: patternBytes(16, 0x10),
                memsz: 16
            },
            {
                type: ptLoad,
                vaddr: 0x20000000,
                paddr: 0x08000000,
                fileData: patternBytes(8, 0x20),
                memsz: 0x20
            }
        ]);

        const segments: LoadSegment[] = readElfLoadSegments(file);

        assert.equal(segments.length, 2);
        assert.equal(segments[0]?.address, 0x08000000);
        assert.deepEqual(Array.from(segments[0]?.data ?? []), Array.from(patternBytes(8, 0x20)), "bss part must not be included");
        assert.equal(segments[1]?.address, 0x08000100);
        assert.deepEqual(Array.from(segments[1]?.data ?? []), Array.from(patternBytes(16, 0x10)));
    });

    it("ignores non-load segments and load segments without file data", (): void => {
        const file: Uint8Array = buildElf32([
            {
                type: ptNote,
                vaddr: 0,
                paddr: 0x08000000,
                fileData: patternBytes(4, 1),
                memsz: 4
            },
            {
                type: ptLoad,
                vaddr: 0x20000000,
                paddr: 0x08000000,
                fileData: new Uint8Array(0),
                memsz: 16
            }
        ]);

        assert.deepEqual(readElfLoadSegments(file), []);
    });

    it("rejects overlapping load segments", (): void => {
        const file: Uint8Array = buildElf32([
            {
                type: ptLoad,
                vaddr: 0x08000000,
                paddr: 0x08000000,
                fileData: patternBytes(8, 1),
                memsz: 8
            },
            {
                type: ptLoad,
                vaddr: 0x08000004,
                paddr: 0x08000004,
                fileData: patternBytes(8, 2),
                memsz: 8
            }
        ]);

        assert.throws((): LoadSegment[] => readElfLoadSegments(file), VbootError);
    });

    it("rejects 64-bit and big-endian files with a clear error", (): void => {
        const file: Uint8Array = buildElf32([
            {
                type: ptLoad,
                vaddr: 0x08000000,
                paddr: 0x08000000,
                fileData: patternBytes(4, 1),
                memsz: 4
            }
        ]);
        const sixtyFourBit: Uint8Array = Uint8Array.from(file);
        const bigEndian: Uint8Array = Uint8Array.from(file);

        sixtyFourBit[4] = 2;
        bigEndian[5] = 2;

        assert.throws((): LoadSegment[] => readElfLoadSegments(sixtyFourBit), VbootError);
        assert.throws((): LoadSegment[] => readElfLoadSegments(bigEndian), VbootError);
    });

    it("rejects data without the ELF magic", (): void => {
        assert.throws((): LoadSegment[] => readElfLoadSegments(new Uint8Array(64)), VbootError);
    });
});


describe("Intel HEX", (): void => {
    it("writes 16-byte data records, extended address records and an EOF record", (): void => {
        const text: string = writeIntelHex(0x08020000, patternBytes(40, 0x55));
        const lines: string[] = text.trim().split(/\r?\n/u);

        assert.equal(lines[0], ":020000040802F0");
        assert.equal(lines[1]?.substring(1, 3), "10");
        assert.equal(lines[2]?.substring(1, 3), "10");
        assert.equal(lines[3]?.substring(1, 3), "08");
        assert.equal(lines[lines.length - 1], ":00000001FF");

        for (const line of lines) {
            assert.ok(line.startsWith(":"));
            assert.equal(checksumValid(line), true, "checksum of " + line);
        }
    });

    it("round-trips data across a 64 KB boundary", (): void => {
        const data: Uint8Array = patternBytes(40, 0x7A);
        const text: string = writeIntelHex(0x0800FFF0, data);
        const flat: FirmwareImage = flattenSegments(readIntelHex(text));

        assert.equal(flat.base, 0x0800FFF0);
        assert.deepEqual(Array.from(flat.data), Array.from(data));
        assert.ok(text.includes(":0200000408" + "01"), "second extended address record expected");
    });
});


describe("FirmwareImage", (): void => {
    let directory: string = "";

    before(async (): Promise<void> => {
        directory = await mkdtemp(path.join(os.tmpdir(), "vboot-image-"));
    });

    after(async (): Promise<void> => {
        await rm(directory, { recursive: true, force: true });
    });

    it("flattens segments from the lowest address and fills gaps with 0xFF", (): void => {
        const flat: FirmwareImage = flattenSegments([
            { address: 0x08000008, data: Uint8Array.from([5, 6, 7, 8]) },
            { address: 0x08000000, data: Uint8Array.from([1, 2, 3, 4]) }
        ]);

        assert.equal(flat.base, 0x08000000);
        assert.deepEqual(Array.from(flat.data), [1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 5, 6, 7, 8]);
    });

    it("refuses segments that are 128 MiB apart instead of allocating the gap", (): void => {
        assert.throws((): FirmwareImage => flattenSegments([
            { address: 0x08000000, data: Uint8Array.from([1]) },
            { address: 0x08000000 + 128 * 1024 * 1024, data: Uint8Array.from([2]) }
        ]), VbootError);
    });

    it("loads a raw binary with the base override, or null without one", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "app.bin");

        await writeFile(file, Uint8Array.from([9, 8, 7]));

        const withBase: FirmwareImage = await loadImage(file, 0x08000000);
        const withoutBase: FirmwareImage = await loadImage(file, null);

        assert.equal(withBase.base, 0x08000000);
        assert.deepEqual(Array.from(withBase.data), [9, 8, 7]);
        assert.equal(withoutBase.base, null);
    });

    it("loads an Intel HEX file with its own base address", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "app.hex");
        const data: Uint8Array = patternBytes(20, 3);

        await writeFile(file, writeIntelHex(0x08001000, data));

        const image: FirmwareImage = await loadImage(file, null);

        assert.equal(image.base, 0x08001000);
        assert.deepEqual(Array.from(image.data), Array.from(data));
    });

    it("refuses a .hex file whose content is not Intel HEX", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "garbage.hex");

        await writeFile(file, "this is not an Intel HEX file\n");

        await assert.rejects(loadImage(file, null), VbootError);
    });

    it("detects an ELF file by its content, whatever the extension", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "app.axf");

        await writeFile(file, buildElf32([
            {
                type: ptLoad,
                vaddr: 0x08000000,
                paddr: 0x08000000,
                fileData: Uint8Array.from([1, 2, 3, 4]),
                memsz: 4
            }
        ]));

        const image: FirmwareImage = await loadImage(file, null);

        assert.equal(image.base, 0x08000000);
        assert.deepEqual(Array.from(image.data), [1, 2, 3, 4]);
    });

    it("loads an ELF file as a flattened image", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "app.elf");

        await writeFile(file, buildElf32([
            {
                type: ptLoad,
                vaddr: 0x08000000,
                paddr: 0x08000000,
                fileData: Uint8Array.from([1, 2, 3, 4]),
                memsz: 4
            },
            {
                type: ptLoad,
                vaddr: 0x08000008,
                paddr: 0x08000008,
                fileData: Uint8Array.from([5, 6, 7, 8]),
                memsz: 4
            }
        ]));

        const image: FirmwareImage = await loadImage(file, null);

        assert.equal(image.base, 0x08000000);
        assert.deepEqual(Array.from(image.data), [1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 5, 6, 7, 8]);
    });
});


describe("mergeFullImage", (): void => {
    const bootloaderData: Uint8Array = patternBytes(0x100, 0x31);
    const bootloader: FirmwareImage = { base: 0x08000000, data: bootloaderData };

    function application(size: number, base: number | null): FirmwareImage {
        return { base: base, data: patternBytes(size, 0x91) };
    }

    it("places both images, fills the rest with 0xFF and writes the header", (): void => {
        const app: FirmwareImage = application(0x1000, regionStart);
        const merged: FirmwareImage = mergeFullImage(bootloader, app, regionStart, regionSize);
        const end: number = regionStart + regionSize;

        assert.equal(merged.base, 0x08000000);
        assert.equal(merged.data.length, end - 0x08000000);
        assert.deepEqual(Array.from(merged.data.subarray(0, 0x100)), Array.from(bootloaderData));

        for (let offset: number = 0x100; offset < regionStart - 0x08000000; offset++) {
            if (merged.data[offset] !== 0xFF) {
                assert.fail("gap byte at offset " + offset + " is not 0xFF");
            }
        }

        const appOffset: number = regionStart - 0x08000000;

        assert.deepEqual(Array.from(merged.data.subarray(appOffset, appOffset + 0x1000)), Array.from(app.data));

        const headerOffset: number = end - 0x08000000 - 8;

        assert.equal(readU32(merged.data, headerOffset), crc32(app.data));
        assert.equal(readU32(merged.data, headerOffset + 4), 0x1000);
    });

    it("accepts an application that ends exactly where the header starts", (): void => {
        const maxSize: number = regionSize - 8;

        assert.doesNotThrow((): FirmwareImage => mergeFullImage(bootloader, application(maxSize, regionStart), regionStart, regionSize));
    });

    it("rejects an application that reaches into the header", (): void => {
        assert.throws(
            (): FirmwareImage => mergeFullImage(bootloader, application(regionSize - 7, regionStart), regionStart, regionSize),
            VbootError
        );
    });

    it("rejects a bootloader that overlaps the application region", (): void => {
        const longBootloader: FirmwareImage = { base: 0x08000000, data: patternBytes(0x20010, 1) };

        assert.throws(
            (): FirmwareImage => mergeFullImage(longBootloader, application(0x100, regionStart), regionStart, regionSize),
            VbootError
        );
    });

    it("rejects an application that is not linked at the region start", (): void => {
        assert.throws(
            (): FirmwareImage => mergeFullImage(bootloader, application(0x100, regionStart + 0x1000), regionStart, regionSize),
            VbootError
        );
    });

    it("rejects images without a known base address", (): void => {
        assert.throws(
            (): FirmwareImage => mergeFullImage({ base: null, data: bootloaderData }, application(0x100, regionStart), regionStart, regionSize),
            VbootError
        );
        assert.throws(
            (): FirmwareImage => mergeFullImage(bootloader, application(0x100, null), regionStart, regionSize),
            VbootError
        );
    });
});
