import { createHash } from "node:crypto";

import { equalBytes } from "../util/Bytes.ts";
import { DeviceError, VbootError } from "../util/Errors.ts";
import type { Log } from "../util/Log.ts";
import type { BootloaderClient } from "./BootloaderClient.ts";
import { Status } from "./Bootloader.ts";
import type { CipherId, DeviceInfo } from "./Bootloader.ts";


export interface FlashOptions {
    start: boolean;
    reset: boolean;
    chunkSize?: number;
}


export interface FlashSource {
    payload: Uint8Array;
    imageCrc32: number;
    imageSize: number;
    iv: Uint8Array | null;
    cipher: CipherId;
    baseAddress: number | null;
}

// AES block size: chunks are multiples of it so that every write stays aligned
const blockSize: number = 16;
const firmwareHeaderSize: number = 8;


function formatAddress(value: number): string {
    return "0x" + value.toString(16).padStart(8, "0");
}


function describeCipher(cipher: CipherId): string {
    if (cipher === "none-or-unknown") {
        return "plain";
    }

    return cipher;
}


function checkCipher(
    deviceCipher: CipherId,
    sourceCipher: CipherId,
    log: Log): void
{
    if (deviceCipher === "none-or-unknown") {
        if (sourceCipher !== "none-or-unknown") {
            log.warn(`the device does not report its cipher; the image is ${describeCipher(sourceCipher)}`);
        }

        return;
    }

    if (deviceCipher !== sourceCipher) {
        throw new VbootError(
            `the device is configured for ${describeCipher(deviceCipher)} but the image is ${describeCipher(sourceCipher)}`);
    }
}


function chooseChunkSize(
    requested: number | undefined,
    packetDataMaxSize: number): number
{
    const size: number = requested ?? packetDataMaxSize;
    // Rounding comes first: the help promises values are rounded down, so the limit applies to the rounded size
    const aligned: number = Math.floor(size / blockSize) * blockSize;
    if (aligned <= 0) {
        throw new VbootError(`chunk size ${size} leaves no whole ${blockSize}-byte block`);
    }

    if (aligned > packetDataMaxSize) {
        throw new VbootError(`chunk size ${aligned} is larger than the device accepts (${packetDataMaxSize} bytes per packet)`);
    }

    return aligned;
}


export async function flash(
    client: BootloaderClient,
    source: FlashSource,
    options: FlashOptions,
    log: Log): Promise<void>
{
    await client.ping();
    const info: DeviceInfo = await client.getInfo();
    log.info(
        `device: region ${formatAddress(info.memoryStart)}, ${info.memorySize} bytes, `
        + `packet data up to ${info.packetDataMaxSize} bytes, cipher ${describeCipher(info.cipher)}`);

    checkCipher(info.cipher, source.cipher, log);
    if (source.baseAddress !== null && source.baseAddress !== info.memoryStart) {
        throw new VbootError(
            `the image was linked for ${formatAddress(source.baseAddress)}, the device region starts at ${formatAddress(info.memoryStart)}`);
    }

    if (source.imageSize > info.memorySize - firmwareHeaderSize) {
        throw new VbootError(
            `the image is ${source.imageSize} bytes, the device region holds ${info.memorySize - firmwareHeaderSize} bytes plus the firmware header`);
    }

    // An invalid chunk size must fail before the device is touched
    const chunkSize: number = chooseChunkSize(options.chunkSize, info.packetDataMaxSize);

    // The IV has to be in place before the image is erased and written
    if (source.iv !== null) {
        await client.setIv(source.iv);
        log.detail("IV set on the device");
    }

    log.info("erasing the firmware region");
    await client.erase();

    log.detail(`writing in chunks of ${chunkSize} bytes`);
    for (let offset: number = 0; offset < source.payload.length; offset += chunkSize) {
        const end: number = Math.min(offset + chunkSize, source.payload.length);
        await client.write(source.payload.subarray(offset, end));
        log.progress("writing", end, source.payload.length);
    }

    const expectedHash: Uint8Array = createHash("sha256").update(source.payload).digest();
    const receivedHash: Uint8Array = await client.getHash();
    if (!equalBytes(receivedHash, expectedHash)) {
        throw new VbootError("the device received different bytes than were sent (hash mismatch)");
    }

    try {
        await client.finalize(source.imageCrc32, source.imageSize);
    } catch (error: unknown) {
        if (error instanceof DeviceError && error.status === Status.ChecksumMismatch) {
            throw new VbootError(
                "the image in flash does not match the package (wrong key or IV on the device?)");
        }

        throw error;
    }

    const valid: boolean = await client.verify();
    if (!valid) {
        throw new VbootError("verify failed: the device does not accept the image in flash");
    }

    log.info("image written and verified");
    if (options.start) {
        await client.start();
        log.info("application started");
    } else if (options.reset) {
        await client.reset();
        log.info("device reset");
    }
}
