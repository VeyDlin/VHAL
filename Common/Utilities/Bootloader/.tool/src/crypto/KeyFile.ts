import { createHash, randomBytes } from "node:crypto";
import { chmodSync, existsSync, lstatSync, readFileSync, writeFileSync } from "node:fs";
import { VbootError } from "../util/Errors.ts";
import { fromHex, toHex } from "../util/Hex.ts";


export type KeyBits = 128 | 192 | 256;


interface KeyFileContent {
    format: string;

    version: number;

    bits: number;

    key: string;

    created: string;

    comment: string;
}

const keyFileFormat: string = "vboot-key";
const keyFileVersion: number = 1;
const keyHexPattern: RegExp = /^[0-9a-fA-F]+$/;
const cIdentifierPattern: RegExp = /^[A-Za-z][A-Za-z0-9_]*$/;
const cValuesPerLine: number = 8;
const keyFileMode: number = 0o600;


export function generateKey(bits: number): Uint8Array {
    if (!isKeyBits(bits)) {
        throw new VbootError(`the key size must be 128, 192 or 256 bits, not ${bits}`);
    }
    return new Uint8Array(randomBytes(bits / 8));
}


// The key file holds the secret in plain text, so it is created with owner-only permissions where the OS supports them.
export function writeKeyFile(
    path: string,
    key: Uint8Array,
    comment: string,
    force: boolean = false): void
{
    const bits: number = key.length * 8;
    if (!isKeyBits(bits)) {
        throw new VbootError(`a key must be 16, 24 or 32 bytes, not ${key.length}`);
    }
    if (!force && existsSync(path)) {
        throw new VbootError(`${path} already exists (pass --force to overwrite it)`);
    }
    if (isSymbolicLink(path)) {
        throw new VbootError(`${path} is a symbolic link; refusing to write the key through it`);
    }

    const content: KeyFileContent = {
        format: keyFileFormat,
        version: keyFileVersion,
        bits,
        key: toHex(key),
        created: new Date().toISOString(),
        comment
    };
    writeFileSync(path, JSON.stringify(content, null, 4) + "\n", { mode: keyFileMode, flag: force ? "w" : "wx" });
    // The mode option only applies when a file is created, so an overwritten file must be restricted explicitly.
    restrictKeyFileMode(path);
}


export function readKeyFile(path: string): Uint8Array {
    const text: string = readFileSync(path, "utf8");
    let parsed: unknown;
    try {
        parsed = JSON.parse(text);
    } catch (error: unknown) {
        throw new VbootError(`${path} is not a valid key file: ${describeError(error)}`);
    }

    if (typeof parsed !== "object" || parsed === null) {
        throw new VbootError(`${path} is not a valid key file`);
    }
    const record: Record<string, unknown> = parsed as Record<string, unknown>;
    if (record.format !== keyFileFormat || record.version !== keyFileVersion) {
        throw new VbootError(`${path} is not a vboot key file (format 1)`);
    }
    if (typeof record.bits !== "number" || !isKeyBits(record.bits)) {
        throw new VbootError(`${path}: "bits" must be 128, 192 or 256`);
    }
    const bits: KeyBits = record.bits;
    if (typeof record.key !== "string" || record.key.length !== bits / 4 || !keyHexPattern.test(record.key)) {
        throw new VbootError(`${path}: "key" must be ${bits / 4} hex digits`);
    }
    if (typeof record.created !== "string" || typeof record.comment !== "string") {
        throw new VbootError(`${path}: "created" and "comment" must be strings`);
    }
    return fromHex(record.key);
}


// The key id is printed and stored in packages so the device owner can tell which key an image was made with.
export function keyId(key: Uint8Array): string {
    return createHash("sha256").update(key).digest("hex").slice(0, 16);
}


export function formatCArray(key: Uint8Array, name: string): string {
    if (!cIdentifierPattern.test(name)) {
        throw new VbootError(`"${name}" is not a valid C identifier`);
    }
    if (key.length === 0) {
        throw new VbootError("the key is empty");
    }

    const rows: string[] = [];
    for (let start: number = 0; start < key.length; start += cValuesPerLine) {
        const values: string[] = [];
        for (const byte of key.subarray(start, start + cValuesPerLine)) {
            values.push(`0x${byte.toString(16).toUpperCase().padStart(2, "0")}`);
        }
        rows.push(`    ${values.join(", ")}`);
    }
    return `static const uint8 ${name}[${key.length}] = {\n${rows.join(",\n")}\n};`;
}


function isKeyBits(value: number): value is KeyBits {
    return value === 128 || value === 192 || value === 256;
}


function describeError(error: unknown): string {
    return error instanceof Error ? error.message : String(error);
}


function errorCode(error: unknown): string | undefined {
    return error instanceof Error ? (error as NodeJS.ErrnoException).code : undefined;
}


function isSymbolicLink(path: string): boolean {
    try {
        return lstatSync(path).isSymbolicLink();
    } catch (error: unknown) {
        if (errorCode(error) === "ENOENT") {
            return false;
        }
        throw error;
    }
}


// Windows only models the read-only flag and some filesystems reject chmod, so those errors are not fatal.
function restrictKeyFileMode(path: string): void {
    try {
        chmodSync(path, keyFileMode);
    } catch (error: unknown) {
        const code: string | undefined = errorCode(error);
        if (code !== "EPERM" && code !== "ENOSYS") {
            throw error;
        }
    }
}
