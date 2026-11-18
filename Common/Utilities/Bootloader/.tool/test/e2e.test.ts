import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { existsSync } from "node:fs";
import { mkdir, mkdtemp, readFile, rm } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { after, before, describe, it } from "node:test";

import { encryptImage } from "../src/crypto/ImageCipher.ts";
import { keyId } from "../src/crypto/KeyFile.ts";
import { BootloaderClient } from "../src/protocol/BootloaderClient.ts";
import type { ClientOptions } from "../src/protocol/BootloaderClient.ts";
import { Status } from "../src/protocol/Bootloader.ts";
import type { CipherId, DeviceInfo } from "../src/protocol/Bootloader.ts";
import { flash } from "../src/protocol/FlashProcedure.ts";
import type { FlashSource } from "../src/protocol/FlashProcedure.ts";
import { decodePackage, encodePackage } from "../src/image/Package.ts";
import type { FirmwarePackage, PackageManifest } from "../src/image/Package.ts";
import { simTransport } from "../src/transport/sim/SimTransport.ts";
import type { Transport } from "../src/transport/Transport.ts";
import { concatBytes, readU32 } from "../src/util/Bytes.ts";
import { crc32 } from "../src/util/Crc32.ts";
import { DeviceError, VbootError } from "../src/util/Errors.ts";
import { fromHex, toHex } from "../src/util/Hex.ts";
import { Log } from "../src/util/Log.ts";


interface EncryptedImage {
    flashImage: Uint8Array;
    payload: Uint8Array;
}


const toolDir: string = path.resolve(import.meta.dirname, "..");
const simCandidates: string[] = [
    path.join(toolDir, "sim", "build", "vboot-sim.exe"),
    path.join(toolDir, "sim", "build", "vboot-sim")
];
const simFound: boolean = simCandidates.some((candidate: string): boolean => existsSync(candidate));
const suiteOptions: { skip?: string; timeout: number } = simFound
    ? { timeout: 120000 }
    : {
        skip: "simulator not built: run `npm run sim:build` to create sim/build/vboot-sim[.exe]",
        timeout: 120000
    };
const regionStart: number = 0x08020000;
const regionSize: number = 0x40000;
const log: Log = new Log("quiet");
// Simulator arguments are split on whitespace, so the temporary files must live on a path without spaces
const tempBase: string = /\s/u.test(os.tmpdir()) ? path.join(toolDir, "out") : os.tmpdir();


function patternBytes(count: number, seed: number): Uint8Array {
    const result: Uint8Array = new Uint8Array(count);

    for (let i: number = 0; i < count; i++) {
        result[i] = (seed + i * 11) & 0xFF;
    }

    return result;
}


function randomOf(length: number): Uint8Array {
    return new Uint8Array(randomBytes(length));
}


function sha256Hex(data: Uint8Array): string {
    return createHash("sha256").update(data).digest("hex");
}


function padTo16(data: Uint8Array): Uint8Array {
    const result: Uint8Array = new Uint8Array(Math.ceil(data.length / 16) * 16).fill(0xFF);

    result.set(data, 0);

    return result;
}


function sleep(ms: number): Promise<void> {
    return new Promise<void>((resolve: () => void): void => {
        setTimeout(resolve, ms);
    });
}


function createClient(simArgs: string): BootloaderClient {
    const options: Map<string, string> = new Map<string, string>([["args", simArgs]]);
    const transport: Transport = simTransport.create(options, log);
    const clientOptions: ClientOptions = {
        commandTimeoutMs: 3000,
        eraseTimeoutMs: 20000,
        retries: 2,
        log: log
    };

    return new BootloaderClient(transport, clientOptions);
}


function cipherIdOf(name: PackageManifest["cipher"]): CipherId {
    if (name === "none") {
        return "none-or-unknown";
    }

    if (name.endsWith("-ecb")) {
        return "aes-ecb";
    }

    if (name.endsWith("-cbc")) {
        return "aes-cbc";
    }

    return "aes-ctr";
}


function plainSource(image: Uint8Array): FlashSource {
    return {
        payload: image,
        imageCrc32: crc32(image),
        imageSize: image.length,
        iv: null,
        cipher: "none-or-unknown",
        baseAddress: regionStart
    };
}


// Builds a .vbp package the way the pack command does: encrypt, fill the manifest, encode
function buildPackage(image: Uint8Array, cipher: "aes-128-cbc" | "aes-256-ctr", key: Uint8Array, iv: Uint8Array): Uint8Array {
    const encrypted: EncryptedImage = encryptImage(image, cipher, key, iv);
    const manifest: PackageManifest = {
        cipher: cipher,
        iv: toHex(iv),
        keyId: keyId(key),
        baseAddress: regionStart,
        image: { size: encrypted.flashImage.length, crc32: crc32(encrypted.flashImage) },
        payload: { size: encrypted.payload.length, sha256: sha256Hex(encrypted.payload) },
        label: "e2e",
        created: new Date(0).toISOString(),
        tool: "vboot test"
    };
    const pkg: FirmwarePackage = { manifest: manifest, payload: encrypted.payload };

    return encodePackage(pkg);
}


// Reads a package back the way the flash command does
function sourceFromPackage(file: Uint8Array): FlashSource {
    const pkg: FirmwarePackage = decodePackage(file);
    const manifest: PackageManifest = pkg.manifest;

    return {
        payload: pkg.payload,
        imageCrc32: manifest.image.crc32,
        imageSize: manifest.image.size,
        iv: manifest.iv === null ? null : fromHex(manifest.iv),
        cipher: cipherIdOf(manifest.cipher),
        baseAddress: manifest.baseAddress
    };
}


async function readBack(client: BootloaderClient): Promise<Uint8Array> {
    const parts: Uint8Array[] = [];
    let offset: number = 0;
    let chunk: Uint8Array | null = await client.readAt(offset);

    while (chunk !== null && chunk.length > 0) {
        parts.push(chunk);
        offset += chunk.length;
        chunk = await client.readAt(offset);
    }

    return concatBytes(parts);
}


// The simulator writes --flash-out when its stdin closes, so the file may appear a moment after close()
async function waitForRegion(file: string): Promise<Uint8Array> {
    const deadline: number = Date.now() + 10000;

    while (Date.now() < deadline) {
        if (existsSync(file)) {
            const data: Uint8Array = new Uint8Array(await readFile(file));

            if (data.length === regionSize) {
                return data;
            }
        }

        await sleep(20);
    }

    throw new Error("the simulator did not write " + file);
}


describe("simulator end to end", suiteOptions, (): void => {
    let tempDir: string = "";

    before(async (): Promise<void> => {
        await mkdir(tempBase, { recursive: true });
        tempDir = await mkdtemp(path.join(tempBase, "vboot-e2e-"));
    });

    after(async (): Promise<void> => {
        await rm(tempDir, { recursive: true, force: true });
    });

    it("flashes a plain image, reads it back and checks the header it wrote", async (): Promise<void> => {
        const outFile: string = path.join(tempDir, "plain.bin");
        const image: Uint8Array = patternBytes(1000, 3);
        const client: BootloaderClient = createClient("--flash-out " + outFile);

        await client.open();

        try {
            await flash(client, plainSource(image), { start: false, reset: false }, log);
            assert.deepEqual(Array.from(await readBack(client)), Array.from(image));
        } finally {
            await client.close();
        }

        const flashed: Uint8Array = await waitForRegion(outFile);

        assert.deepEqual(Array.from(flashed.subarray(0, image.length)), Array.from(image));
        assert.equal(readU32(flashed, regionSize - 8), crc32(image));
        assert.equal(readU32(flashed, regionSize - 4), image.length);
    });

    it("reports the cipher and packet size in GetInfo", async (): Promise<void> => {
        const plain: BootloaderClient = createClient("");

        await plain.open();

        let plainInfo: DeviceInfo;

        try {
            plainInfo = await plain.getInfo();
        } finally {
            await plain.close();
        }

        assert.equal(plainInfo.cipher, "none-or-unknown");
        assert.equal(plainInfo.packetDataMaxSize, 240);
        assert.equal(plainInfo.memoryStart, regionStart);
        assert.equal(plainInfo.memorySize, regionSize);

        const aes: BootloaderClient = createClient("--cipher aes-256-ctr --key " + toHex(randomOf(32)));

        await aes.open();

        let aesInfo: DeviceInfo;

        try {
            aesInfo = await aes.getInfo();
        } finally {
            await aes.close();
        }

        assert.equal(aesInfo.cipher, "aes-ctr");
        assert.equal(aesInfo.packetDataMaxSize, 240);
    });

    it("refuses a plain image on a device that expects AES-CTR", async (): Promise<void> => {
        const client: BootloaderClient = createClient("--cipher aes-256-ctr --key " + toHex(randomOf(32)));

        await client.open();

        try {
            // The host-side cipher check must refuse before the device answers; a device refusal is not enough
            await assert.rejects(
                flash(client, plainSource(patternBytes(64, 1)), { start: false, reset: false }, log),
                (error: unknown): boolean => error instanceof VbootError
                    && !(error instanceof DeviceError)
                    && /configured for aes-ctr but the image is plain/u.test(error.message)
            );
        } finally {
            await client.close();
        }
    });

    it("installs an AES-256-CTR package with a random IV that differs from the constructor IV", async (): Promise<void> => {
        const outFile: string = path.join(tempDir, "ctr.bin");
        const key: Uint8Array = randomOf(32);
        const deviceIv: Uint8Array = new Uint8Array(16).fill(0x11);
        const packageIv: Uint8Array = randomOf(16);
        const image: Uint8Array = patternBytes(1000, 5);
        const source: FlashSource = sourceFromPackage(buildPackage(image, "aes-256-ctr", key, packageIv));
        const client: BootloaderClient = createClient(
            "--cipher aes-256-ctr --key " + toHex(key) + " --iv " + toHex(deviceIv) + " --flash-out " + outFile
        );

        await client.open();

        try {
            await flash(client, source, { start: false, reset: false }, log);
            assert.equal(await client.verify(), true);
        } finally {
            await client.close();
        }

        const flashed: Uint8Array = await waitForRegion(outFile);

        assert.deepEqual(Array.from(flashed.subarray(0, image.length)), Array.from(image));
    });

    it("installs an AES-128-CBC package with padding and a random IV", async (): Promise<void> => {
        const outFile: string = path.join(tempDir, "cbc.bin");
        const key: Uint8Array = randomOf(16);
        const packageIv: Uint8Array = randomOf(16);
        const image: Uint8Array = patternBytes(1000, 7);
        const padded: Uint8Array = padTo16(image);
        const source: FlashSource = sourceFromPackage(buildPackage(image, "aes-128-cbc", key, packageIv));
        const client: BootloaderClient = createClient(
            "--cipher aes-128-cbc --key " + toHex(key) + " --flash-out " + outFile
        );

        await client.open();

        try {
            await flash(client, source, { start: false, reset: false }, log);
            assert.equal(await client.verify(), true);
        } finally {
            await client.close();
        }

        const flashed: Uint8Array = await waitForRegion(outFile);

        assert.equal(readU32(flashed, regionSize - 4), padded.length);
        assert.deepEqual(Array.from(flashed.subarray(0, padded.length)), Array.from(padded));
    });

    it("refuses Read on an AES device", async (): Promise<void> => {
        const client: BootloaderClient = createClient("--cipher aes-256-ctr --key " + toHex(randomOf(32)));

        await client.open();

        try {
            await assert.rejects(client.readAt(0), (error: unknown): boolean => {
                return error instanceof DeviceError && error.status === Status.Protected;
            });
        } finally {
            await client.close();
        }
    });

    it("reports a checksum mismatch and a failed verify for a package made with the wrong key", async (): Promise<void> => {
        const deviceKey: Uint8Array = randomOf(32);
        const packageKey: Uint8Array = randomOf(32);
        const packageIv: Uint8Array = randomOf(16);
        const source: FlashSource = sourceFromPackage(buildPackage(patternBytes(512, 9), "aes-256-ctr", packageKey, packageIv));
        const client: BootloaderClient = createClient("--cipher aes-256-ctr --key " + toHex(deviceKey));

        await client.open();

        try {
            await assert.rejects(
                flash(client, source, { start: false, reset: false }, log),
                (error: unknown): boolean => {
                    return error instanceof VbootError && /key/iu.test(error.message);
                }
            );
            assert.equal(await client.verify(), false);
        } finally {
            await client.close();
        }
    });

    it("accepts a repeated Finalize with --strict-program", async (): Promise<void> => {
        const image: Uint8Array = patternBytes(500, 13);
        const client: BootloaderClient = createClient("--strict-program");

        await client.open();

        try {
            await flash(client, plainSource(image), { start: false, reset: false }, log);
            await client.finalize(crc32(image), image.length);
            assert.equal(await client.verify(), true);
        } finally {
            await client.close();
        }
    });

    it("erase removes the image: no data is read back and the header is gone", async (): Promise<void> => {
        const image: Uint8Array = patternBytes(300, 17);
        const client: BootloaderClient = createClient("");

        await client.open();

        try {
            await flash(client, plainSource(image), { start: false, reset: false }, log);
            await client.erase();
            assert.equal((await readBack(client)).length, 0);
            assert.equal(await client.verify(), false);
        } finally {
            await client.close();
        }
    });

    it("stores user data per type", async (): Promise<void> => {
        const client: BootloaderClient = createClient("--user-data");

        await client.open();

        try {
            await client.userDataWrite(0x05, Uint8Array.from([1, 2, 3, 4]));
            await client.userDataWrite(0x06, Uint8Array.from([9]));

            assert.deepEqual(Array.from(await client.userDataRead(0x05)), [1, 2, 3, 4]);
            assert.deepEqual(Array.from(await client.userDataRead(0x06)), [9]);
        } finally {
            await client.close();
        }
    });
});
