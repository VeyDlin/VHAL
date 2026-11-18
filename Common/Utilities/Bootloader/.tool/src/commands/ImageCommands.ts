import { writeFile } from "node:fs/promises";
import { extname } from "node:path";
import { Command } from "commander";
import { type FirmwareImage, loadImage } from "../image/FirmwareImage.ts";
import { mergeFullImage } from "../image/Merge.ts";
import { writeIntelHex } from "../image/IntelHex.ts";
import { VbootError } from "../util/Errors.ts";
import type { Log } from "../util/Log.ts";
import { formatHex32, logOf, parseIntegerArgument, runCommand } from "./CommandContext.ts";


interface ImageCommandOptions {
    output: string;
    base?: number;
}


interface MergeCommandOptions {
    bootloader: string;
    application: string;
    regionStart: number;
    regionSize: number;
    output: string;
    bootloaderBase?: number;
    applicationBase?: number;
}


export function registerImageCommands(program: Command): void {
    addImageCommand(program);
    addMergeCommand(program);
}


function addImageCommand(program: Command): void {
    program
        .command("image")
        .description("convert an ELF or Intel HEX file to a raw binary or Intel HEX file")
        .argument("<file>", "input file: an ELF or Intel HEX file (detected by content), or a raw binary")
        .requiredOption("-o, --output <file>", "output file: .bin or .hex")
        .option("--base <address>", "load address of a raw binary (ELF and HEX files carry their own)", parseIntegerArgument)
        .addHelpText(
            "after",
            `
The input format is taken from its content, not its name: an ELF file is an ELF file whatever its extension.
A file named .elf or .hex that does not hold that format is an error. Anything else is a raw binary.
A raw binary has no address. Writing Intel HEX needs one, so pass --base for a raw input.

Examples:
  $ vboot image app.elf -o app.bin
  $ vboot image app.elf -o app.hex
  $ vboot image app.bin -o app.hex --base 0x08020000
`,
        )
        .action(async (file: string, options: ImageCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const log: Log = logOf(command);
                const outputKind: OutputKind = outputKindOf(options.output);
                const image: FirmwareImage = await loadImage(file, options.base ?? null);

                await writeImage(options.output, outputKind, image);
                log.info(describeImage(options.output, image));
            });
        });
}


function addMergeCommand(program: Command): void {
    program
        .command("merge")
        .description("place a bootloader and an application into one factory image with the firmware header")
        .requiredOption("--bootloader <file>", "bootloader image (ELF, Intel HEX detected by content, or raw binary)")
        .requiredOption("--application <file>", "application image (ELF, Intel HEX detected by content, or raw binary)")
        .requiredOption("--region-start <address>", "start of the application region (the application must be linked here)", parseIntegerArgument)
        .requiredOption("--region-size <bytes>", "size of the application region, the header is its last 8 bytes", parseIntegerArgument)
        .requiredOption("-o, --output <file>", "output file: .bin or .hex")
        .option("--bootloader-base <address>", "load address of a raw bootloader binary", parseIntegerArgument)
        .option("--application-base <address>", "load address of a raw application binary", parseIntegerArgument)
        .addHelpText(
            "after",
            `
The result starts at the bootloader address, is 0xFF where no image is, and ends with the 8-byte firmware
header {crc32, size} of the application, so the bootloader accepts the application without an upload.

Example:
  $ vboot merge --bootloader boot.elf --application app.elf --region-start 0x08020000 --region-size 0x40000 -o factory.bin
`,
        )
        .action(async (options: MergeCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const log: Log = logOf(command);
                const outputKind: OutputKind = outputKindOf(options.output);
                const bootloader: FirmwareImage = await loadImage(options.bootloader, options.bootloaderBase ?? null);
                const application: FirmwareImage = await loadImage(options.application, options.applicationBase ?? null);
                const merged: FirmwareImage = mergeFullImage(bootloader, application, options.regionStart, options.regionSize);

                await writeImage(options.output, outputKind, merged);
                log.info(describeImage(options.output, merged));
            });
        });
}


type OutputKind = "bin" | "hex";


// Checked before any work starts, so a wrong output name fails immediately
function outputKindOf(output: string): OutputKind {
    const extension: string = extname(output).toLowerCase();
    if (extension === ".bin") {
        return "bin";
    }

    if (extension === ".hex") {
        return "hex";
    }

    throw new VbootError(`the output must be a .bin or .hex file, got "${output}"`);
}


async function writeImage(output: string, kind: OutputKind, image: FirmwareImage): Promise<void> {
    if (kind === "bin") {
        await writeFile(output, image.data);
        return;
    }

    if (image.base === null) {
        throw new VbootError("Intel HEX needs a base address, pass --base for a raw input");
    }

    await writeFile(output, writeIntelHex(image.base, image.data), "utf8");
}


function describeImage(output: string, image: FirmwareImage): string {
    const base: string = image.base === null ? "none (raw binary)" : formatHex32(image.base);

    return `wrote ${output}: base ${base}, ${image.data.length} bytes`;
}
