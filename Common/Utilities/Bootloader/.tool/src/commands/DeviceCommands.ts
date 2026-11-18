import { readFile, writeFile } from "node:fs/promises";
import { extname } from "node:path";
import { Command, Option } from "commander";
import type { CipherId, DeviceInfo, DeviceStatus } from "../protocol/Bootloader.ts";
import { BootloaderClient } from "../protocol/BootloaderClient.ts";
import { flash, type FlashOptions, type FlashSource } from "../protocol/FlashProcedure.ts";
import { loadImage, type FirmwareImage } from "../image/FirmwareImage.ts";
import { decodePackage, type FirmwarePackage, type PackageManifest } from "../image/Package.ts";
import type { DiscoveredDevice, TransportFactory } from "../transport/Transport.ts";
import { createRegistry } from "../transport/transports.ts";
import { concatBytes } from "../util/Bytes.ts";
import { VbootError } from "../util/Errors.ts";
import { crc32 } from "../util/Crc32.ts";
import { fromHex, toHex } from "../util/Hex.ts";
import type { Log } from "../util/Log.ts";
import {
    type DeviceSettings,
    formatHex32,
    parseByteArgument,
    parseIntegerArgument,
    printLines,
    readDeviceSettings,
    runCommand,
    withDevice,
} from "./CommandContext.ts";


interface FlashCommandOptions {
    start?: boolean;
    reset?: boolean;
    chunk?: number;
    base?: number;
}


interface ReadCommandOptions {
    output: string;
    offset: number;
    length?: number;
}


interface UnlockCommandOptions {
    password?: string;
    passwordFile?: string;
}


interface ScanCommandOptions {
    duration: number;
}


export function registerDeviceCommands(program: Command): void {
    addTransportsCommand(program);
    addScanCommand(program);
    addPingCommand(program);
    addInfoCommand(program);
    addStatusCommand(program);
    addHashCommand(program);
    addEraseCommand(program);
    addVerifyCommand(program);
    addStartCommand(program);
    addResetCommand(program);
    addFlashCommand(program);
    addReadCommand(program);
    addUnlockCommand(program);
    addUserDataCommand(program);
    addSetIvCommand(program);
}


function addTransportsCommand(program: Command): void {
    program
        .command("transports")
        .description("list the transports and their options")
        .addHelpText("after", "\nExample:\n  $ vboot transports\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const lines: string[] = [];
                for (const factory of createRegistry().list()) {
                    lines.push(`${factory.name}  ${factory.description}`);
                    for (const option of factory.options) {
                        const defaultText: string = option.defaultValue === undefined ? "" : ` (default: ${option.defaultValue})`;
                        lines.push(`    --${factory.name}-${option.name} <value>  ${option.description}${defaultText}`);
                    }
                }

                printLines(lines);
            });
        });
}


function addScanCommand(program: Command): void {
    program
        .command("scan")
        .description("list the devices the selected transport can see")
        .option("--duration <ms>", "scan time", parseIntegerArgument, 5000)
        .addHelpText("after", "\nExamples:\n  $ vboot -t ble scan\n  $ vboot -t ble scan --duration 10000\n")
        .action(async (options: ScanCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const settings: DeviceSettings = readDeviceSettings(command);
                const factory: TransportFactory = settings.factory;
                if (factory.scan === undefined) {
                    throw new VbootError(`transport "${factory.name}" cannot scan for devices`);
                }

                const devices: DiscoveredDevice[] = await factory.scan(settings.transportOptions, options.duration, settings.log);
                if (devices.length === 0) {
                    settings.log.info("no devices found");
                    return;
                }

                printLines(devices.map((device: DiscoveredDevice): string => {
                    const rssi: string = device.rssi === undefined ? "" : `  rssi ${device.rssi}`;

                    return `${device.id}  ${device.name}${rssi}`;
                }));
            });
        });
}


function addPingCommand(program: Command): void {
    program
        .command("ping")
        .description("check that the bootloader answers")
        .addHelpText("after", "\nExample:\n  $ vboot ping\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.ping();
                    log.info("bootloader answered");
                });
            });
        });
}


function addInfoCommand(program: Command): void {
    program
        .command("info")
        .description("show the bootloader version, flash layout, read protection and cipher")
        .addHelpText("after", "\nExample:\n  $ vboot info\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient): Promise<void> => {
                    const info: DeviceInfo = await client.getInfo();
                    printLines(describeInfo(info));
                });
            });
        });
}


function addStatusCommand(program: Command): void {
    program
        .command("status")
        .description("show the device state and the number of bytes received for the current image")
        .addHelpText("after", "\nExample:\n  $ vboot status\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient): Promise<void> => {
                    const status: DeviceStatus = await client.getStatus();
                    printLines([
                        `state: ${String(status.state)}`,
                        `bytes received: ${status.bytesReceived}`,
                    ]);
                });
            });
        });
}


function addHashCommand(program: Command): void {
    program
        .command("hash")
        .description("show the SHA-256 of the raw data received for the current image")
        .addHelpText("after", "\nExample:\n  $ vboot hash\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient): Promise<void> => {
                    const hash: Uint8Array = await client.getHash();
                    printLines([`sha256: ${toHex(hash)}`]);
                });
            });
        });
}


function addEraseCommand(program: Command): void {
    program
        .command("erase")
        .description("erase the application area (a new image starts; the device answers when the erase is done)")
        .addHelpText("after", "\nExample:\n  $ vboot erase\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    log.info("erasing, this can take a while");
                    await client.erase();
                    log.info("erased");
                });
            });
        });
}


function addVerifyCommand(program: Command): void {
    program
        .command("verify")
        .description("ask the device whether the image in flash is valid (fails when it is not)")
        .addHelpText("after", "\nExample:\n  $ vboot verify\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    const valid: boolean = await client.verify();
                    if (!valid) {
                        throw new VbootError("the device reports that the image in flash is not valid");
                    }

                    log.info("image is valid");
                });
            });
        });
}


function addStartCommand(program: Command): void {
    program
        .command("start")
        .description("start the application (the device jumps to it after answering)")
        .addHelpText("after", "\nExample:\n  $ vboot start\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.start();
                    log.info("start accepted, the device jumps to the application");
                });
            });
        });
}


function addResetCommand(program: Command): void {
    program
        .command("reset")
        .description("reset the device (the device answers, then reboots)")
        .addHelpText("after", "\nExample:\n  $ vboot reset\n")
        .action(async (options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.reset();
                    log.info("reset accepted, the device reboots");
                });
            });
        });
}


function addFlashCommand(program: Command): void {
    program
        .command("flash")
        .description("erase, write, check and finalize an image, then optionally start or reset the device")
        .argument("<file>", "package (.vbp) or plain image: ELF or Intel HEX (detected by content), otherwise a raw binary")
        .addOption(new Option("--start", "start the application after the update").conflicts("reset"))
        .addOption(new Option("--reset", "reset the device after the update"))
        .option("--chunk <bytes>", "bytes per Write command, rounded down to a multiple of 16 (default: the device maximum)", parseIntegerArgument)
        .option("--base <address>", "load address of a plain image (.bin); .elf and .hex files carry their own", parseIntegerArgument)
        .addHelpText(
            "after",
            `
A package (.vbp) carries its cipher, IV and checksums; the device must be configured with the same cipher.
A plain image is sent as it is (cipher none) and its CRC-32 and size are computed here.

Examples:
  $ vboot flash app.vbp --start
  $ vboot flash app.bin --base 0x08020000 --reset
`,
        )
        .action(async (file: string, options: FlashCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const source: FlashSource = await buildFlashSource(file, options.base);
                const flashOptions: FlashOptions = {
                    start: options.start === true,
                    reset: options.reset === true,
                    chunkSize: options.chunk,
                };

                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await flash(client, source, flashOptions, log);
                });
            });
        });
}


function addReadCommand(program: Command): void {
    program
        .command("read")
        .description("read back the image written since the device started (Read with offsets)")
        .requiredOption("-o, --output <file>", "file to write")
        .option("--offset <bytes>", "offset in the written image to start at", parseIntegerArgument, 0)
        .option("--length <bytes>", "stop after this many bytes (default: until the device reports end of data)", parseIntegerArgument)
        .addHelpText(
            "after",
            `
The device only reads back what was written since it started: Read answers end of data for anything
else, and it is refused while the AES modes are used (the flash is encrypted).

Examples:
  $ vboot read -o dump.bin
  $ vboot read -o part.bin --offset 240 --length 1024
`,
        )
        .action(async (options: ReadCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    const data: Uint8Array = await readImage(client, log, options.offset, options.length);
                    await writeFile(options.output, data);
                    log.info(`read ${data.length} bytes to ${options.output}`);
                });
            });
        });
}


function addUnlockCommand(program: Command): void {
    program
        .command("unlock")
        .description("remove read-out protection (RDP) with the device password")
        .addOption(new Option("--password <hex>", "password as hex").conflicts("passwordFile"))
        .addOption(new Option("--password-file <file>", "file containing the password as hex text").conflicts("password"))
        .addHelpText("after", "\nExample:\n  $ vboot unlock --password-file device-password.txt\n")
        .action(async (options: UnlockCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const password: Uint8Array = await readPassword(options);
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.unlock(password);
                    log.info("unlock accepted (the device answers the same way when it was not protected)");
                });
            });
        });
}


function addUserDataCommand(program: Command): void {
    const userData: Command = program
        .command("user-data")
        .description("read or write user data entries (battery values, IVs, ...)");

    userData
        .command("get")
        .description("read the user data entry of a type")
        .argument("<type>", "data type, 0-255", parseByteArgument)
        .addHelpText("after", "\nExample:\n  $ vboot user-data get 1\n")
        .action(async (type: number, options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await withDevice(command, async (client: BootloaderClient): Promise<void> => {
                    const data: Uint8Array = await client.userDataRead(type);
                    printLines([toHex(data)]);
                });
            });
        });

    userData
        .command("set")
        .description("write the user data entry of a type")
        .argument("<type>", "data type, 0-255", parseByteArgument)
        .argument("<hex>", "data as hex")
        .addHelpText("after", "\nExample:\n  $ vboot user-data set 1 0a0b\n")
        .action(async (type: number, hex: string, options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const data: Uint8Array = fromHex(hex);
                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.userDataWrite(type, data);
                    log.info(`user data type ${type} written (${data.length} bytes)`);
                });
            });
        });
}


function addSetIvCommand(program: Command): void {
    program
        .command("set-iv")
        .description("set the IV for the next image (AES CBC and CTR; flash only uses it when the image is sent)")
        .argument("<hex>", "16-byte IV as hex")
        .addHelpText("after", "\nExample:\n  $ vboot set-iv 000102030405060708090a0b0c0d0e0f\n")
        .action(async (hex: string, options: object, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const iv: Uint8Array = fromHex(hex);
                if (iv.length !== 16) {
                    throw new VbootError(`the IV must be 16 bytes, got ${iv.length}`);
                }

                await withDevice(command, async (client: BootloaderClient, log: Log): Promise<void> => {
                    await client.setIv(iv);
                    log.info("IV set");
                });
            });
        });
}


function describeInfo(info: DeviceInfo): string[] {
    const protection: string = info.protectionLevel === 255 ? "unknown" : `level ${info.protectionLevel}`;

    return [
        `bootloader version: 0x${info.bootloaderVersion.toString(16)}`,
        `memory start: ${formatHex32(info.memoryStart)}`,
        `memory size: ${info.memorySize} bytes (0x${info.memorySize.toString(16)})`,
        `max packet size (transport): ${info.maxPacketSize}`,
        `packet data max size: ${info.packetDataMaxSize}`,
        `read protection: ${protection}`,
        `access: ${info.accessDenied ? "denied" : "allowed"}`,
        `cipher: ${info.cipher}`,
    ];
}


// A package carries everything the device needs; a plain image is sent as it is
async function buildFlashSource(file: string, base: number | undefined): Promise<FlashSource> {
    if (extname(file).toLowerCase() === ".vbp") {
        if (base !== undefined) {
            throw new VbootError("--base applies to plain images only, a package records its base address");
        }

        const pkg: FirmwarePackage = decodePackage(await readFile(file));

        return {
            payload: pkg.payload,
            imageCrc32: pkg.manifest.image.crc32,
            imageSize: pkg.manifest.image.size,
            iv: pkg.manifest.iv === null ? null : fromHex(pkg.manifest.iv),
            cipher: cipherIdOf(pkg.manifest.cipher),
            baseAddress: pkg.manifest.baseAddress,
        };
    }

    const image: FirmwareImage = await loadImage(file, base ?? null);

    return {
        payload: image.data,
        imageCrc32: crc32(image.data),
        imageSize: image.data.length,
        iv: null,
        cipher: "none-or-unknown",
        baseAddress: image.base,
    };
}


function cipherIdOf(cipher: PackageManifest["cipher"]): CipherId {
    if (cipher === "none") {
        return "none-or-unknown";
    }

    if (cipher.endsWith("-ecb")) {
        return "aes-ecb";
    }

    if (cipher.endsWith("-cbc")) {
        return "aes-cbc";
    }

    return "aes-ctr";
}


// Reads with increasing offsets until the device reports end of data or the requested length is reached
async function readImage(client: BootloaderClient, log: Log, offset: number, length: number | undefined): Promise<Uint8Array> {
    const parts: Uint8Array[] = [];
    let total: number = 0;
    let position: number = offset;

    while (length === undefined || total < length) {
        const chunk: Uint8Array | null = await client.readAt(position);
        if (chunk === null || chunk.length === 0) {
            break;
        }

        let part: Uint8Array = chunk;
        if (length !== undefined && total + part.length > length) {
            part = part.subarray(0, length - total);
        }

        parts.push(part);
        total += part.length;
        position += part.length;
        if (length !== undefined) {
            log.progress("reading", total, length);
        }
    }

    if (length !== undefined && total < length) {
        log.warn(`the device has only ${total} bytes from offset ${offset}, ${length} were requested`);
    }

    return concatBytes(parts);
}


async function readPassword(options: UnlockCommandOptions): Promise<Uint8Array> {
    if (options.password !== undefined) {
        return fromHex(options.password);
    }

    if (options.passwordFile !== undefined) {
        const text: string = await readFile(options.passwordFile, "utf8");

        return fromHex(text.replace(/\s+/g, ""));
    }

    throw new VbootError("give the password with --password <hex> or --password-file <file>");
}
