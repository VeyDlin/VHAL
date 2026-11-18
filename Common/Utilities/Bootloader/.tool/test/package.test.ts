import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { describe, it } from "node:test";

import { decodePackage, encodePackage } from "../src/image/Package.ts";
import type { FirmwarePackage, PackageManifest } from "../src/image/Package.ts";
import { VbootError } from "../src/util/Errors.ts";
import { readU32 } from "../src/util/Bytes.ts";
import { crc32 } from "../src/util/Crc32.ts";


function sha256Hex(data: Uint8Array): string {
    return createHash("sha256").update(data).digest("hex");
}


function payloadBytes(count: number): Uint8Array {
    const result: Uint8Array = new Uint8Array(count);

    for (let i: number = 0; i < count; i++) {
        result[i] = (i * 31 + 5) & 0xFF;
    }

    return result;
}


function samplePackage(): FirmwarePackage {
    const payload: Uint8Array = payloadBytes(1008);
    const manifest: PackageManifest = {
        cipher: "aes-256-ctr",
        iv: "00112233445566778899aabbccddeeff",
        keyId: "0123456789abcdef",
        baseAddress: 0x08020000,
        image: { size: 1008, crc32: 0x12345678 },
        payload: { size: payload.length, sha256: sha256Hex(payload) },
        label: "test image",
        created: "2026-01-02T03:04:05.000Z",
        tool: "vboot 1.0.0"
    };

    return { manifest: manifest, payload: payload };
}


function expectVbootError(action: () => unknown): void {
    assert.throws(action, VbootError);
}


describe("package encoding", (): void => {
    it("writes the fixed header, the manifest and the payload in that order", (): void => {
        const pkg: FirmwarePackage = samplePackage();
        const file: Uint8Array = encodePackage(pkg);
        const manifestLength: number = readU32(file, 8);
        const manifestText: string = new TextDecoder().decode(file.subarray(12, 12 + manifestLength));

        assert.equal(new TextDecoder().decode(file.subarray(0, 4)), "VBPK");
        assert.equal(file[4], 1);
        assert.equal(file[5], 0);
        assert.equal(file[6], 0);
        assert.equal(file[7], 0);
        assert.equal(JSON.parse(manifestText).cipher, "aes-256-ctr");
        assert.equal(file.length, 12 + manifestLength + pkg.payload.length);
        assert.deepEqual(Array.from(file.subarray(12 + manifestLength)), Array.from(pkg.payload));
    });

    it("round-trips the manifest and the payload", (): void => {
        const pkg: FirmwarePackage = samplePackage();
        const decoded: FirmwarePackage = decodePackage(encodePackage(pkg));

        assert.deepEqual(decoded.manifest, pkg.manifest);
        assert.deepEqual(Array.from(decoded.payload), Array.from(pkg.payload));
    });

    it("keeps null fields as null for a plain image", (): void => {
        const payload: Uint8Array = payloadBytes(20);
        const pkg: FirmwarePackage = {
            manifest: {
                cipher: "none",
                iv: null,
                keyId: null,
                baseAddress: null,
                image: { size: 20, crc32: crc32(payload) },
                payload: { size: 20, sha256: sha256Hex(payload) },
                label: null,
                created: "2026-01-02T03:04:05.000Z",
                tool: "vboot 1.0.0"
            },
            payload: payload
        };
        const decoded: FirmwarePackage = decodePackage(encodePackage(pkg));

        assert.equal(decoded.manifest.cipher, "none");
        assert.equal(decoded.manifest.iv, null);
        assert.equal(decoded.manifest.keyId, null);
        assert.equal(decoded.manifest.baseAddress, null);
        assert.equal(decoded.manifest.label, null);
    });
});


describe("package decoding checks", (): void => {
    it("rejects a wrong magic", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());

        file[0] = 0x58;
        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects an unsupported format version", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());

        file[4] = 2;
        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects a truncated file", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());

        expectVbootError((): FirmwarePackage => decodePackage(file.subarray(0, file.length - 1)));
    });

    it("rejects data after the payload", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());
        const extended: Uint8Array = new Uint8Array(file.length + 1);

        extended.set(file, 0);
        expectVbootError((): FirmwarePackage => decodePackage(extended));
    });

    it("rejects a payload that does not match its sha256", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());

        file[file.length - 1] = (file[file.length - 1] ?? 0) ^ 0x01;
        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects a manifest length larger than the file", (): void => {
        const file: Uint8Array = encodePackage(samplePackage());
        const view: DataView = new DataView(file.buffer);

        view.setUint32(8, file.length, true);
        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects a manifest that is not JSON", (): void => {
        const junk: Uint8Array = new TextEncoder().encode("not json at all");
        const file: Uint8Array = new Uint8Array(12 + junk.length);
        const view: DataView = new DataView(file.buffer);

        file.set(new TextEncoder().encode("VBPK"), 0);
        view.setUint16(4, 1, true);
        view.setUint32(8, junk.length, true);
        file.set(junk, 12);
        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects a file shorter than the fixed header", (): void => {
        expectVbootError((): FirmwarePackage => decodePackage(new Uint8Array(4)));
    });

    it("rejects a plain image whose image.crc32 does not match its payload", (): void => {
        const payload: Uint8Array = payloadBytes(64);
        const pkg: FirmwarePackage = {
            manifest: {
                cipher: "none",
                iv: null,
                keyId: null,
                baseAddress: null,
                image: { size: 64, crc32: 0x12345678 },
                payload: { size: 64, sha256: sha256Hex(payload) },
                label: null,
                created: "2026-01-02T03:04:05.000Z",
                tool: "vboot 1.0.0"
            },
            payload: payload
        };
        const file: Uint8Array = encodePackage(pkg);

        expectVbootError((): FirmwarePackage => decodePackage(file));
    });

    it("rejects a CBC package whose payload is not a whole number of 16-byte blocks", (): void => {
        const payload: Uint8Array = payloadBytes(1001);
        const pkg: FirmwarePackage = {
            manifest: {
                cipher: "aes-128-cbc",
                iv: "00112233445566778899aabbccddeeff",
                keyId: "0123456789abcdef",
                baseAddress: 0x08020000,
                image: { size: 1001, crc32: 0 },
                payload: { size: 1001, sha256: sha256Hex(payload) },
                label: null,
                created: "2026-01-02T03:04:05.000Z",
                tool: "vboot 1.0.0"
            },
            payload: payload
        };
        const file: Uint8Array = encodePackage(pkg);

        expectVbootError((): FirmwarePackage => decodePackage(file));
    });
});
