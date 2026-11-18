import { encodeFirmwareHeader } from "../protocol/Bootloader.ts";
import { crc32 } from "../util/Crc32.ts";
import { VbootError } from "../util/Errors.ts";
import type { FirmwareImage } from "./FirmwareImage.ts";


const firmwareHeaderSize: number = 8;
const addressSpaceSize: number = 0x1_0000_0000;


// Builds the factory image: bootloader and application at their bases, 0xFF elsewhere, and the firmware header
// in the last 8 bytes of the region, so the bootloader accepts the application without an upload.
export function mergeFullImage(
    bootloader: FirmwareImage,
    application: FirmwareImage,
    regionStart: number,
    regionSize: number): FirmwareImage
{
    if (bootloader.base === null) {
        throw new VbootError("the bootloader has no base address (pass --bootloader-base)");
    }
    if (application.base === null) {
        throw new VbootError("the application has no base address (pass --application-base)");
    }
    if (application.base !== regionStart) {
        throw new VbootError(
            `the application is linked at 0x${application.base.toString(16)}, the region starts at 0x${regionStart.toString(16)}`);
    }
    if (regionSize < firmwareHeaderSize || regionStart + regionSize > addressSpaceSize) {
        throw new VbootError(`the region 0x${regionStart.toString(16)} + 0x${regionSize.toString(16)} is invalid`);
    }
    if (bootloader.data.length === 0 || application.data.length === 0) {
        throw new VbootError("the bootloader and the application must not be empty");
    }

    const headerAddress: number = regionStart + regionSize - firmwareHeaderSize;
    const bootloaderEnd: number = bootloader.base + bootloader.data.length;
    const applicationEnd: number = regionStart + application.data.length;
    if (applicationEnd > headerAddress) {
        throw new VbootError(
            `the application ends at 0x${applicationEnd.toString(16)}, after the firmware header at 0x${headerAddress.toString(16)}`);
    }
    if (bootloader.base < applicationEnd && regionStart < bootloaderEnd) {
        throw new VbootError("the bootloader and the application overlap");
    }
    if (bootloaderEnd > headerAddress) {
        throw new VbootError(`the bootloader ends at 0x${bootloaderEnd.toString(16)}, inside the firmware header`);
    }

    const imageStart: number = Math.min(bootloader.base, regionStart);
    const data: Uint8Array = new Uint8Array(regionStart + regionSize - imageStart).fill(0xff);
    data.set(bootloader.data, bootloader.base - imageStart);
    data.set(application.data, regionStart - imageStart);
    data.set(encodeFirmwareHeader(crc32(application.data), application.data.length), headerAddress - imageStart);
    return { base: imageStart, data };
}
