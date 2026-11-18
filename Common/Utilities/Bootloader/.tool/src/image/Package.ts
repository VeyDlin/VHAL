import { createHash } from "node:crypto";
import { concatBytes } from "../util/Bytes.ts";
import { VbootError } from "../util/Errors.ts";
import { crc32 } from "../util/Crc32.ts";


export type PackageCipher = "none" | `aes-${128 | 192 | 256}-${"ecb" | "cbc" | "ctr"}`;


export interface PackageImageInfo {
    size: number;

    crc32: number;
}


export interface PackagePayloadInfo {
    size: number;

    sha256: string;
}


export interface PackageManifest {
    cipher: PackageCipher;

    iv: string | null;

    keyId: string | null;

    baseAddress: number | null;

    image: PackageImageInfo;

    payload: PackagePayloadInfo;

    label: string | null;

    created: string;

    tool: string;
}


export interface FirmwarePackage {
    manifest: PackageManifest;

    payload: Uint8Array;
}

const packageMagic: string = "VBPK";
const packageFormatVersion: number = 1;
const packageHeaderSize: number = 12;
const maxSafeSize: number = Number.MAX_SAFE_INTEGER;
const uint32Max: number = 0xffff_ffff;
const aesBlockSize: number = 16;
const cipherPattern: RegExp = /^(none|aes-(128|192|256)-(ecb|cbc|ctr))$/;
const ivPattern: RegExp = /^[0-9a-f]{32}$/;
const keyIdPattern: RegExp = /^[0-9a-f]{16}$/;
const sha256Pattern: RegExp = /^[0-9a-f]{64}$/;


export function sha256Hex(data: Uint8Array): string {
    return createHash("sha256").update(data).digest("hex");
}


export function encodePackage(pkg: FirmwarePackage): Uint8Array {
    if (pkg.manifest.payload.size !== pkg.payload.length || pkg.manifest.payload.sha256 !== sha256Hex(pkg.payload)) {
        throw new VbootError("the package manifest does not describe its payload");
    }

    const manifestBytes: Uint8Array = new TextEncoder().encode(JSON.stringify(pkg.manifest, null, 2));
    const header: Uint8Array = new Uint8Array(packageHeaderSize);
    const view: DataView = new DataView(header.buffer);
    header.set(new TextEncoder().encode(packageMagic), 0);
    view.setUint16(4, packageFormatVersion, true);
    view.setUint16(6, 0, true);
    view.setUint32(8, manifestBytes.length, true);
    return concatBytes([header, manifestBytes, pkg.payload]);
}


export function decodePackage(file: Uint8Array): FirmwarePackage {
    if (file.length < packageHeaderSize) {
        throw new VbootError("not a vboot package: the file is too short");
    }
    if (new TextDecoder().decode(file.subarray(0, 4)) !== packageMagic) {
        throw new VbootError("not a vboot package: bad magic");
    }

    const view: DataView = new DataView(file.buffer, file.byteOffset, file.byteLength);
    const formatVersion: number = view.getUint16(4, true);
    if (formatVersion !== packageFormatVersion) {
        throw new VbootError(`unsupported package format version ${formatVersion}`);
    }
    if (view.getUint16(6, true) !== 0) {
        throw new VbootError("not a vboot package: the reserved header field is not zero");
    }
    const manifestLength: number = view.getUint32(8, true);
    const payloadOffset: number = packageHeaderSize + manifestLength;
    if (payloadOffset > file.length) {
        throw new VbootError("the package manifest lies outside the file");
    }

    const manifest: PackageManifest = parseManifest(parseManifestJson(file.subarray(packageHeaderSize, payloadOffset)));
    const payload: Uint8Array = file.slice(payloadOffset);
    if (manifest.payload.size !== payload.length) {
        throw new VbootError(`the package payload is ${payload.length} bytes, the manifest says ${manifest.payload.size}`);
    }
    if (manifest.payload.sha256 !== sha256Hex(payload)) {
        throw new VbootError("the package payload is corrupted: SHA-256 does not match the manifest");
    }
    // Only cipher none stores the image as it is, so only there can image.crc32 be checked without the key.
    if (manifest.cipher === "none" && crc32(payload) !== manifest.image.crc32) {
        throw new VbootError("package manifest: \"image.crc32\" does not match the payload, which is the image for cipher none");
    }
    return { manifest, payload };
}


function parseManifestJson(bytes: Uint8Array): unknown {
    try {
        return JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(bytes));
    } catch (error: unknown) {
        throw new VbootError(`the package manifest is not valid UTF-8 JSON: ${describeError(error)}`);
    }
}


function parseManifest(value: unknown): PackageManifest {
    const record: Record<string, unknown> = readRecord(value, "manifest");
    const cipher: PackageCipher = readCipher(record.cipher);

    const requiresIv: boolean = cipher.endsWith("-cbc") || cipher.endsWith("-ctr");
    let iv: string | null = null;
    if (requiresIv) {
        iv = readPattern(record.iv, ivPattern, "iv", "32 lowercase hex digits");
    } else if (record.iv !== null) {
        throw manifestError("iv", "null for this cipher");
    }

    const requiresKey: boolean = cipher !== "none";
    let keyId: string | null = null;
    if (requiresKey) {
        keyId = readPattern(record.keyId, keyIdPattern, "keyId", "16 lowercase hex digits");
    } else if (record.keyId !== null) {
        throw manifestError("keyId", "null for cipher none");
    }

    const image: PackageImageInfo = readImageInfo(record.image);
    const payload: PackagePayloadInfo = readPayloadInfo(record.payload);
    if (payload.size !== image.size) {
        throw new VbootError("the package payload size differs from the image size");
    }
    const usesBlocks: boolean = cipher.endsWith("-ecb") || cipher.endsWith("-cbc");
    if (usesBlocks && image.size % aesBlockSize !== 0) {
        throw new VbootError("the package image size is not a multiple of the AES block size");
    }

    return {
        cipher,
        iv,
        keyId,
        baseAddress: readNullableUint(record.baseAddress, "baseAddress", uint32Max),
        image,
        payload,
        label: readNullableString(record.label, "label"),
        created: readString(record.created, "created"),
        tool: readString(record.tool, "tool")
    };
}


function readImageInfo(value: unknown): PackageImageInfo {
    const record: Record<string, unknown> = readRecord(value, "image");
    return {
        size: readUint(record.size, "image.size", maxSafeSize),
        crc32: readUint(record.crc32, "image.crc32", uint32Max)
    };
}


function readPayloadInfo(value: unknown): PackagePayloadInfo {
    const record: Record<string, unknown> = readRecord(value, "payload");
    return {
        size: readUint(record.size, "payload.size", maxSafeSize),
        sha256: readPattern(record.sha256, sha256Pattern, "payload.sha256", "64 lowercase hex digits")
    };
}


function readCipher(value: unknown): PackageCipher {
    if (isPackageCipher(value)) {
        return value;
    }
    throw manifestError("cipher", "a known cipher name");
}


function isPackageCipher(value: unknown): value is PackageCipher {
    return typeof value === "string" && cipherPattern.test(value);
}


function readRecord(value: unknown, field: string): Record<string, unknown> {
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
        throw manifestError(field, "an object");
    }
    return value as Record<string, unknown>;
}


function readString(value: unknown, field: string): string {
    if (typeof value === "string") {
        return value;
    }
    throw manifestError(field, "a string");
}


function readNullableString(value: unknown, field: string): string | null {
    if (value === null) {
        return null;
    }
    return readString(value, field);
}


function readPattern(value: unknown, pattern: RegExp, field: string, description: string): string {
    if (typeof value === "string" && pattern.test(value)) {
        return value;
    }
    throw manifestError(field, description);
}


function readUint(value: unknown, field: string, max: number): number {
    if (typeof value === "number" && Number.isSafeInteger(value) && value >= 0 && value <= max) {
        return value;
    }
    throw manifestError(field, `an integer from 0 to ${max}`);
}


function readNullableUint(value: unknown, field: string, max: number): number | null {
    if (value === null) {
        return null;
    }
    return readUint(value, field, max);
}


function manifestError(field: string, expected: string): VbootError {
    return new VbootError(`package manifest: "${field}" must be ${expected}`);
}


function describeError(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}
