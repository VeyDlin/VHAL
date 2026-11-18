import assert from "node:assert/strict";
import { describe, it } from "node:test";

import { concatBytes, equalBytes, readU32, readU64, u32Bytes, writeU32 } from "../src/util/Bytes.ts";
import { VbootError } from "../src/util/Errors.ts";
import { crc32 } from "../src/util/Crc32.ts";
import { fromHex, parseInteger, toHex } from "../src/util/Hex.ts";


function bytesOf(text: string): Uint8Array {
    return new TextEncoder().encode(text);
}


describe("crc32", (): void => {
    it("matches the standard check value", (): void => {
        assert.equal(crc32(bytesOf("123456789")), 0xCBF43926);
    });

    it("is zero for empty input", (): void => {
        assert.equal(crc32(new Uint8Array(0)), 0);
    });

    it("matches a well-known sentence", (): void => {
        assert.equal(crc32(bytesOf("The quick brown fox jumps over the lazy dog")), 0x414FA339);
    });

    it("continues from a previous value", (): void => {
        const head: number = crc32(bytesOf("12345"));

        assert.equal(crc32(bytesOf("6789"), head), 0xCBF43926);
    });
});


describe("hex", (): void => {
    it("formats bytes as lowercase hex without separators", (): void => {
        assert.equal(toHex(new Uint8Array([0x00, 0xAB, 0xFF, 0x10])), "00abff10");
        assert.equal(toHex(new Uint8Array(0)), "");
    });

    it("parses an optional 0x prefix, spaces, colons and commas", (): void => {
        assert.deepEqual(Array.from(fromHex("0xDEADbeef")), [0xDE, 0xAD, 0xBE, 0xEF]);
        assert.deepEqual(Array.from(fromHex("de ad:be,ef")), [0xDE, 0xAD, 0xBE, 0xEF]);
        assert.deepEqual(Array.from(fromHex("0x01 02:03,04")), [1, 2, 3, 4]);
    });

    it("round-trips through toHex", (): void => {
        const data: Uint8Array = new Uint8Array([0, 1, 127, 128, 254, 255]);

        assert.deepEqual(Array.from(fromHex(toHex(data))), Array.from(data));
    });

    it("rejects an odd number of digits and bad characters", (): void => {
        assert.throws((): Uint8Array => fromHex("abc"), VbootError);
        assert.throws((): Uint8Array => fromHex("zz"), VbootError);
        assert.throws((): Uint8Array => fromHex("0x12 3"), VbootError);
    });

    it("parses decimal and hexadecimal integers", (): void => {
        assert.equal(parseInteger("42"), 42);
        assert.equal(parseInteger("0"), 0);
        assert.equal(parseInteger("0x1F"), 31);
        assert.equal(parseInteger("0x08020000"), 0x08020000);
    });

    it("rejects malformed integers", (): void => {
        assert.throws((): number => parseInteger(""), VbootError);
        assert.throws((): number => parseInteger("12abc"), VbootError);
        assert.throws((): number => parseInteger("0xZZ"), VbootError);
    });

    it("rejects a leading zero on a decimal, which is easy to mistake for hex", (): void => {
        assert.throws((): number => parseInteger("08020000"), VbootError);
    });
});


describe("bytes", (): void => {
    it("concatenates parts in order", (): void => {
        const joined: Uint8Array = concatBytes([bytesOf("ab"), new Uint8Array(0), bytesOf("cd")]);

        assert.deepEqual(Array.from(joined), [0x61, 0x62, 0x63, 0x64]);
        assert.equal(concatBytes([]).length, 0);
    });

    it("reads little-endian u32 values unsigned", (): void => {
        assert.equal(readU32(new Uint8Array([0x78, 0x56, 0x34, 0x12]), 0), 0x12345678);
        assert.equal(readU32(new Uint8Array([0, 0, 1, 2, 3, 4]), 2), 0x04030201);
        assert.equal(readU32(new Uint8Array([0xFF, 0xFF, 0xFF, 0xFF]), 0), 4294967295);
    });

    it("writes little-endian u32 values at an offset", (): void => {
        const target: Uint8Array = new Uint8Array(5);

        writeU32(target, 1, 0xA1B2C3D4);

        assert.deepEqual(Array.from(target), [0x00, 0xD4, 0xC3, 0xB2, 0xA1]);
    });

    it("reads little-endian u64 values as bigint", (): void => {
        const data: Uint8Array = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);

        assert.equal(readU64(data, 0), 0x0807060504030201n);
    });

    it("encodes u32 values as four little-endian bytes", (): void => {
        assert.deepEqual(Array.from(u32Bytes(0x08020000)), [0x00, 0x00, 0x02, 0x08]);
    });

    it("compares byte arrays by content and length", (): void => {
        assert.equal(equalBytes(bytesOf("abc"), bytesOf("abc")), true);
        assert.equal(equalBytes(bytesOf("abc"), bytesOf("abd")), false);
        assert.equal(equalBytes(bytesOf("abc"), bytesOf("ab")), false);
    });
});
