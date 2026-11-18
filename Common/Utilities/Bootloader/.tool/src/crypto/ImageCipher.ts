import { createCipheriv, randomBytes } from "node:crypto";
import type { Cipheriv } from "node:crypto";
import { readFileSync } from "node:fs";
import type { FirmwareImage } from "../image/FirmwareImage.ts";
import type { FirmwarePackage, PackageCipher, PackageManifest } from "../image/Package.ts";
import { sha256Hex } from "../image/Package.ts";
import { crc32 } from "../util/Crc32.ts";
import { VbootError } from "../util/Errors.ts";
import { toHex } from "../util/Hex.ts";
import { keyId } from "./KeyFile.ts";


export interface EncryptedImage {
    flashImage: Uint8Array;

    payload: Uint8Array;
}


export interface PackOptions {
    cipher: PackageCipher;

    key: Uint8Array | null;

    iv: Uint8Array | null;

    label: string | null;
}

const aesBlockSize: number = 16;
const ivSize: number = 16;
const paddingByte: number = 0xff;
const aesCipherPattern: RegExp = /^aes-(128|192|256)-(ecb|cbc|ctr)$/;


// flashImage is what the device stores after decryption and what Finalize checks. ECB and CBC pad the image with 0xFF
// to whole blocks; CTR is a stream and is not padded. The counter of CTR is the IV plus the block index, as a
// big-endian 128-bit number, which is what the device computes.
export function encryptImage(
    image: Uint8Array,
    cipher: PackageCipher,
    key: Uint8Array | null,
    iv: Uint8Array | null): EncryptedImage
{
    if (image.length === 0) {
        throw new VbootError("the image is empty");
    }
    if (cipher === "none") {
        if (key !== null || iv !== null) {
            throw new VbootError("cipher none takes neither a key nor an IV");
        }
        return { flashImage: image, payload: image };
    }

    const match: RegExpExecArray | null = aesCipherPattern.exec(cipher);
    if (match === null) {
        throw new VbootError(`unknown cipher "${cipher}"`);
    }
    const bits: number = Number(match[1]);
    const mode: string = match[2] ?? "";
    if (key === null || key.length !== bits / 8) {
        throw new VbootError(`cipher ${cipher} needs a ${bits}-bit key`);
    }
    const isEcb: boolean = mode === "ecb";
    if (isEcb && iv !== null) {
        throw new VbootError(`cipher ${cipher} takes no IV`);
    }
    if (!isEcb && (iv === null || iv.length !== ivSize)) {
        throw new VbootError(`cipher ${cipher} needs a 16-byte IV`);
    }

    const algorithm: string = `aes-${bits}-${mode}`;
    if (mode === "ctr") {
        return { flashImage: image, payload: runCipher(algorithm, key, iv, image) };
    }

    const paddedLength: number = Math.ceil(image.length / aesBlockSize) * aesBlockSize;
    const padded: Uint8Array = new Uint8Array(paddedLength).fill(paddingByte);
    padded.set(image);
    return { flashImage: padded, payload: runCipher(algorithm, key, iv, padded) };
}


// The manifest is filled from the image and the payload. A CBC or CTR package without an IV gets a random one.
export function createPackage(
    image: FirmwareImage,
    options: PackOptions): FirmwarePackage
{
    const needsIv: boolean = options.cipher.endsWith("-cbc") || options.cipher.endsWith("-ctr");
    const iv: Uint8Array | null = needsIv ? (options.iv ?? new Uint8Array(randomBytes(ivSize))) : options.iv;
    const encrypted: EncryptedImage = encryptImage(image.data, options.cipher, options.key, iv);

    const manifest: PackageManifest = {
        cipher: options.cipher,
        iv: iv === null ? null : toHex(iv),
        keyId: options.key === null ? null : keyId(options.key),
        baseAddress: image.base,
        image: {
            size: encrypted.flashImage.length,
            crc32: crc32(encrypted.flashImage)
        },
        payload: {
            size: encrypted.payload.length,
            sha256: sha256Hex(encrypted.payload)
        },
        label: options.label,
        created: new Date().toISOString(),
        tool: `vboot ${readToolVersion()}`
    };
    return { manifest, payload: encrypted.payload };
}


function runCipher(
    algorithm: string,
    key: Uint8Array,
    iv: Uint8Array | null,
    input: Uint8Array): Uint8Array
{
    const cipher: Cipheriv = createCipheriv(algorithm, key, iv);
    cipher.setAutoPadding(false);
    const output: Buffer = Buffer.concat([cipher.update(input), cipher.final()]);
    return new Uint8Array(output);
}


function readToolVersion(): string {
    const text: string = readFileSync(new URL("../../package.json", import.meta.url), "utf8");
    const parsed: unknown = JSON.parse(text);
    if (typeof parsed === "object" && parsed !== null && "version" in parsed && typeof parsed.version === "string") {
        return parsed.version;
    }
    throw new VbootError("package.json has no version");
}
