import { readFile, writeFile } from "node:fs/promises";
import { Command } from "commander";
import { createPackage } from "../crypto/ImageCipher.ts";
import { readKeyFile } from "../crypto/KeyFile.ts";
import { loadImage, type FirmwareImage } from "../image/FirmwareImage.ts";
import {
    decodePackage,
    encodePackage,
    type FirmwarePackage,
    type PackageCipher,
    type PackageManifest,
} from "../image/Package.ts";
import { VbootError } from "../util/Errors.ts";
import { fromHex } from "../util/Hex.ts";
import type { Log } from "../util/Log.ts";
import {
    formatHex32,
    logOf,
    parseIntegerArgument,
    printLines,
    runCommand,
} from "./CommandContext.ts";


interface PackCommandOptions {
    output: string;
    cipher?: string;
    key?: string;
    iv?: string;
    base?: number;
    label?: string;
}


interface InspectCommandOptions {
    json?: boolean;
}

const cipherNamePattern: RegExp = /^(none|aes-(128|192|256)-(ecb|cbc|ctr))$/;


export function registerPackageCommands(program: Command): void {
    addPackCommand(program);
    addInspectCommand(program);
}


function addPackCommand(program: Command): void {
    program
        .command("pack")
        .description("make a firmware package (.vbp) from a plain image, optionally encrypted with an AES key")
        .argument("<image>", "plain image: ELF or Intel HEX (detected by content), otherwise a raw binary")
        .requiredOption("-o, --output <file.vbp>", "package file to write")
        .option("--cipher <name>", "none, or aes-<128|192|256>-<ecb|cbc|ctr> (default: none, or aes-<key bits>-ctr with --key)")
        .option("--key <file>", "key file made by 'vboot key generate'")
        .option("--iv <hex>", "16-byte IV for CBC and CTR (default: random)")
        .option("--base <address>", "flash address the image is linked for (plain images; .elf and .hex carry their own)", parseIntegerArgument)
        .option("--label <text>", "free text stored in the package")
        .addHelpText(
            "after",
            `
The image is padded with 0xFF to a multiple of 16 for the block ciphers. The package stores the bytes
as they end up in flash (the image) and the bytes sent to the device (the payload). The IV is not secret.

Examples:
  $ vboot key generate -o app.key
  $ vboot pack app.bin -o app.vbp --key app.key --base 0x08020000
  $ vboot pack app.bin -o app-plain.vbp
`,
        )
        .action(async (imagePath: string, options: PackCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                await packImage(imagePath, options, logOf(command));
            });
        });
}


function addInspectCommand(program: Command): void {
    program
        .command("inspect")
        .description("show the manifest of a firmware package")
        .argument("<file.vbp>", "package file")
        .option("--json", "print the manifest as JSON")
        .addHelpText("after", "\nExamples:\n  $ vboot inspect app.vbp\n  $ vboot inspect app.vbp --json\n")
        .action(async (file: string, options: InspectCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const pkg: FirmwarePackage = decodePackage(await readFile(file));
                if (options.json === true) {
                    console.log(JSON.stringify(pkg.manifest, null, 4));
                    return;
                }

                printLines(describePackage(file, pkg.manifest));
            });
        });
}


async function packImage(imagePath: string, options: PackCommandOptions, log: Log): Promise<void> {
    const key: Uint8Array | null = options.key === undefined ? null : readKeyFile(options.key);
    const cipher: PackageCipher = resolveCipher(options.cipher, key);
    const iv: Uint8Array | null = resolveIv(cipher, options.iv);

    const image: FirmwareImage = await loadImage(imagePath, options.base ?? null);
    if (options.base !== undefined && image.base !== null && image.base !== options.base) {
        throw new VbootError(`the file is linked for ${formatHex32(image.base)}, --base says ${formatHex32(options.base)}`);
    }

    // createPackage makes a random IV for CBC and CTR when none is given
    const pkg: FirmwarePackage = createPackage(image, {
        cipher: cipher,
        key: key,
        iv: iv,
        label: options.label ?? null,
    });
    await writeFile(options.output, encodePackage(pkg));

    log.info(`wrote ${options.output}`);
    for (const line of describePackage(options.output, pkg.manifest)) {
        log.info(line);
    }
}


// The cipher name is validated here so that a typo cannot reach the package encoder
function resolveCipher(name: string | undefined, key: Uint8Array | null): PackageCipher {
    let text: string = "none";
    if (name !== undefined) {
        text = name;
    } else if (key !== null) {
        text = `aes-${key.length * 8}-ctr`;
    }

    if (!cipherNamePattern.test(text)) {
        throw new VbootError(`unknown cipher "${text}", use none or aes-<128|192|256>-<ecb|cbc|ctr>`);
    }

    if (text === "none" && key !== null) {
        throw new VbootError("--key needs an AES cipher, pass --cipher aes-<bits>-<ecb|cbc|ctr>");
    }

    if (text !== "none" && key === null) {
        throw new VbootError(`cipher ${text} needs a key, pass --key <file>`);
    }

    if (key !== null && key.length * 8 !== aesKeyBits(text)) {
        throw new VbootError(`cipher ${text} needs a ${aesKeyBits(text)}-bit key, the key file has ${key.length * 8} bits`);
    }

    return text as PackageCipher;
}


function resolveIv(cipher: PackageCipher, ivHex: string | undefined): Uint8Array | null {
    if (cipher === "none" || cipher.endsWith("-ecb")) {
        if (ivHex !== undefined) {
            throw new VbootError(`${cipher} takes no IV`);
        }

        return null;
    }

    if (ivHex === undefined) {
        return null;
    }

    const iv: Uint8Array = fromHex(ivHex);
    if (iv.length !== 16) {
        throw new VbootError(`the IV must be 16 bytes, got ${iv.length}`);
    }

    return iv;
}


function aesKeyBits(cipher: string): number {
    return Number(cipher.split("-")[1]);
}


function describePackage(file: string, manifest: PackageManifest): string[] {
    return [
        `file: ${file}`,
        `cipher: ${manifest.cipher}`,
        `key id: ${manifest.keyId ?? "none"}`,
        `iv: ${manifest.iv ?? "none"}`,
        `base address: ${manifest.baseAddress === null ? "unknown" : formatHex32(manifest.baseAddress)}`,
        `label: ${manifest.label ?? "none"}`,
        `created: ${manifest.created}`,
        `tool: ${manifest.tool}`,
        `image: ${manifest.image.size} bytes, crc32 ${formatHex32(manifest.image.crc32)}`,
        `payload: ${manifest.payload.size} bytes, sha256 ${manifest.payload.sha256}`,
    ];
}
