import assert from "node:assert/strict";
import { type Cipheriv, createCipheriv, createHash, randomBytes } from "node:crypto";
import { existsSync, type Stats } from "node:fs";
import { chmod, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { encryptImage } from "../src/crypto/ImageCipher.ts";
import { formatCArray, generateKey, keyId, readKeyFile, writeKeyFile } from "../src/crypto/KeyFile.ts";
import { VbootError } from "../src/util/Errors.ts";
import { fromHex, toHex } from "../src/util/Hex.ts";


interface EncryptedImage {
    flashImage: Uint8Array;
    payload: Uint8Array;
}


// A pending promise must fail its test instead of hanging the whole run
const asyncTestOptions: { timeout: number } = { timeout: 10000 };
// Owner-only modes are not enforced on Windows, so these tests are reported as skipped there
const posixTestOptions: { skip: string | false; timeout: number } = {
    skip: process.platform === "win32" ? "file modes are not enforced on Windows" : false,
    timeout: 10000
};


function patternBytes(count: number, seed: number): Uint8Array {
    const result: Uint8Array = new Uint8Array(count);

    for (let i: number = 0; i < count; i++) {
        result[i] = (seed + i * 17) & 0xFF;
    }

    return result;
}


function keyBytes(length: number): Uint8Array {
    return new Uint8Array(randomBytes(length));
}


function ecbEncrypt(key: Uint8Array, data: Uint8Array): Uint8Array {
    const algorithm: string = "aes-" + key.length * 8 + "-ecb";
    const cipher: Cipheriv = createCipheriv(algorithm, key, null);

    cipher.setAutoPadding(false);

    return new Uint8Array(Buffer.concat([cipher.update(data), cipher.final()]));
}


function cbcEncrypt(key: Uint8Array, iv: Uint8Array, data: Uint8Array): Uint8Array {
    const algorithm: string = "aes-" + key.length * 8 + "-cbc";
    const cipher: Cipheriv = createCipheriv(algorithm, key, iv);

    cipher.setAutoPadding(false);

    return new Uint8Array(Buffer.concat([cipher.update(data), cipher.final()]));
}


function counterBlock(value: bigint): Uint8Array {
    const hex: string = value.toString(16).padStart(32, "0");

    return fromHex(hex);
}


// Independent CTR reference: keystream block i is AES-ECB of (IV + i) as a 128-bit big-endian counter
function ctrReference(key: Uint8Array, iv: Uint8Array, data: Uint8Array): Uint8Array {
    const mask: bigint = (1n << 128n) - 1n;
    const start: bigint = BigInt("0x" + toHex(iv));
    const blockCount: number = Math.ceil(data.length / 16);
    const keystream: Uint8Array = new Uint8Array(blockCount * 16);

    for (let i: number = 0; i < blockCount; i++) {
        const block: Uint8Array = ecbEncrypt(key, counterBlock((start + BigInt(i)) & mask));

        keystream.set(block, i * 16);
    }

    const result: Uint8Array = new Uint8Array(data.length);

    for (let i: number = 0; i < data.length; i++) {
        result[i] = (data[i] ?? 0) ^ (keystream[i] ?? 0);
    }

    return result;
}


function padTo16(data: Uint8Array): Uint8Array {
    const length: number = Math.ceil(data.length / 16) * 16;
    const result: Uint8Array = new Uint8Array(length).fill(0xFF);

    result.set(data, 0);

    return result;
}


describe("ImageCipher", (): void => {
    it("passes a plain image through unchanged", (): void => {
        const image: Uint8Array = patternBytes(37, 1);
        const encrypted: EncryptedImage = encryptImage(image, "none", null, null);

        assert.deepEqual(Array.from(encrypted.flashImage), Array.from(image));
        assert.deepEqual(Array.from(encrypted.payload), Array.from(image));
    });

    it("pads ECB images with 0xFF to a multiple of 16 and encrypts the padded image", (): void => {
        const key: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(20, 2);
        const encrypted: EncryptedImage = encryptImage(image, "aes-128-ecb", key, null);

        assert.equal(encrypted.flashImage.length, 32);
        assert.deepEqual(Array.from(encrypted.flashImage), Array.from(padTo16(image)));
        assert.equal(encrypted.flashImage[31], 0xFF);
        assert.deepEqual(Array.from(encrypted.payload), Array.from(ecbEncrypt(key, padTo16(image))));
    });

    it("adds no padding block when the image is already a multiple of 16", (): void => {
        const key: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(32, 3);
        const encrypted: EncryptedImage = encryptImage(image, "aes-128-ecb", key, null);

        assert.equal(encrypted.flashImage.length, 32);
        assert.equal(encrypted.payload.length, 32);
    });

    it("pads CBC images and matches an independent CBC encryption with the IV", (): void => {
        const key: Uint8Array = keyBytes(24);
        const iv: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(33, 4);
        const encrypted: EncryptedImage = encryptImage(image, "aes-192-cbc", key, iv);

        assert.equal(encrypted.flashImage.length, 48);
        assert.deepEqual(Array.from(encrypted.flashImage), Array.from(padTo16(image)));
        assert.deepEqual(Array.from(encrypted.payload), Array.from(cbcEncrypt(key, iv, padTo16(image))));
    });

    it("keeps CTR images unpadded and matches the block-by-block reference", (): void => {
        const key: Uint8Array = keyBytes(32);
        const iv: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(21, 5);
        const encrypted: EncryptedImage = encryptImage(image, "aes-256-ctr", key, iv);

        assert.deepEqual(Array.from(encrypted.flashImage), Array.from(image));
        assert.equal(encrypted.payload.length, 21);
        assert.deepEqual(Array.from(encrypted.payload), Array.from(ctrReference(key, iv, image)));
    });

    it("carries the CTR counter across the 128-bit boundary like the reference", (): void => {
        const key: Uint8Array = keyBytes(16);
        const iv: Uint8Array = new Uint8Array(16).fill(0xFF);

        iv[15] = 0xFE;

        const image: Uint8Array = patternBytes(48, 6);
        const encrypted: EncryptedImage = encryptImage(image, "aes-128-ctr", key, iv);

        assert.deepEqual(Array.from(encrypted.payload), Array.from(ctrReference(key, iv, image)));
    });

    it("decrypts to the original image when the same keystream is applied again", (): void => {
        const key: Uint8Array = keyBytes(16);
        const iv: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(100, 7);
        const once: EncryptedImage = encryptImage(image, "aes-128-ctr", key, iv);
        const twice: EncryptedImage = encryptImage(once.payload, "aes-128-ctr", key, iv);

        assert.deepEqual(Array.from(twice.payload), Array.from(image));
    });

    it("refuses a key whose size does not match the cipher name", (): void => {
        const image: Uint8Array = patternBytes(16, 8);

        assert.throws((): EncryptedImage => encryptImage(image, "aes-256-ctr", keyBytes(16), keyBytes(16)), VbootError);
        assert.throws((): EncryptedImage => encryptImage(image, "aes-128-cbc", keyBytes(32), keyBytes(16)), VbootError);
    });

    it("uses different ciphertext for different IVs", (): void => {
        const key: Uint8Array = keyBytes(16);
        const image: Uint8Array = patternBytes(64, 9);
        const first: EncryptedImage = encryptImage(image, "aes-128-ctr", key, keyBytes(16));
        const second: EncryptedImage = encryptImage(image, "aes-128-ctr", key, keyBytes(16));

        assert.notDeepEqual(Array.from(first.payload), Array.from(second.payload));
    });
});


describe("KeyFile", (): void => {
    let directory: string = "";

    before(async (): Promise<void> => {
        directory = await mkdtemp(path.join(os.tmpdir(), "vboot-key-"));
    });

    after(async (): Promise<void> => {
        await rm(directory, { recursive: true, force: true });
    });

    it("generates keys of 16, 24 and 32 bytes", (): void => {
        assert.equal(generateKey(128).length, 16);
        assert.equal(generateKey(192).length, 24);
        assert.equal(generateKey(256).length, 32);
        assert.notDeepEqual(Array.from(generateKey(128)), Array.from(generateKey(128)));
    });

    it("writes a key file that reads back to the same key", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "roundtrip.key");
        const key: Uint8Array = generateKey(256);

        await writeKeyFile(file, key, "test key");

        const text: string = await readFile(file, "utf8");
        const parsed: Record<string, unknown> = JSON.parse(text) as Record<string, unknown>;

        assert.equal(parsed["format"], "vboot-key");
        assert.equal(parsed["version"], 1);
        assert.equal(parsed["bits"], 256);
        assert.equal(parsed["key"], toHex(key));
        assert.equal(parsed["comment"], "test key");
        assert.deepEqual(Array.from(readKeyFile(file)), Array.from(key));
    });

    it("refuses to overwrite an existing key file", asyncTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "exists.key");

        await writeKeyFile(file, generateKey(128), "first");
        await assert.rejects(async (): Promise<void> => {
            await writeKeyFile(file, generateKey(128), "second");
        }, VbootError);
        assert.equal(existsSync(file), true);
    });

    it("restricts the key file to its owner where the platform supports it", posixTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "mode.key");

        await writeKeyFile(file, generateKey(128), "mode");

        const info: Stats = await stat(file);

        assert.equal(info.mode & 0o777, 0o600);
    });

    it("restricts an overwritten key file to its owner", posixTestOptions, async (): Promise<void> => {
        const file: string = path.join(directory, "forced.key");

        await writeFile(file, "old contents");
        // Start from a looser mode so the test fails if the overwrite keeps it
        await chmod(file, 0o644);
        await writeKeyFile(file, generateKey(128), "forced", true);

        const info: Stats = await stat(file);

        assert.equal(info.mode & 0o777, 0o600);
    });

    it("rejects a key file with the wrong format, a size that does not match bits, or bad hex", asyncTestOptions, async (): Promise<void> => {
        const wrongFormat: string = path.join(directory, "wrong-format.key");
        const wrongSize: string = path.join(directory, "wrong-size.key");
        const badHex: string = path.join(directory, "bad-hex.key");

        await writeFile(wrongFormat, JSON.stringify({ format: "other", version: 1, bits: 128, key: toHex(keyBytes(16)) }));
        await writeFile(wrongSize, JSON.stringify({ format: "vboot-key", version: 1, bits: 256, key: toHex(keyBytes(16)) }));
        await writeFile(badHex, JSON.stringify({ format: "vboot-key", version: 1, bits: 128, key: "zz" }));

        assert.throws((): Uint8Array => readKeyFile(wrongFormat), VbootError);
        assert.throws((): Uint8Array => readKeyFile(wrongSize), VbootError);
        assert.throws((): Uint8Array => readKeyFile(badHex), VbootError);
    });

    it("derives a key id from the first 8 bytes of SHA-256(key)", (): void => {
        const key: Uint8Array = keyBytes(16);
        const digest: string = createHash("sha256").update(key).digest("hex");

        assert.equal(keyId(key), digest.substring(0, 16));
    });

    it("formats a C array with the requested name and length", (): void => {
        const key: Uint8Array = keyBytes(16);
        const text: string = formatCArray(key, "kImageKey");
        const values: RegExpMatchArray | null = text.match(/0x[0-9a-fA-F]{2}/gu);

        assert.ok(text.includes("static const uint8 kImageKey[16] = {"));
        assert.equal(values?.length, 16);

        for (const line of text.split("\n")) {
            const perLine: RegExpMatchArray | null = line.match(/0x[0-9a-fA-F]{2}/gu);

            assert.ok((perLine?.length ?? 0) <= 8, "at most 8 values per line");
        }
    });
});
