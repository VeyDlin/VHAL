import { Command, InvalidArgumentError } from "commander";
import { formatCArray, generateKey, keyId, readKeyFile, writeKeyFile } from "../crypto/KeyFile.ts";
import { VbootError } from "../util/Errors.ts";
import { toHex } from "../util/Hex.ts";
import type { Log } from "../util/Log.ts";
import { logOf, parseIntegerArgument, printLines, runCommand } from "./CommandContext.ts";


interface GenerateCommandOptions {
    output: string;
    bits: number;
    comment: string;
    force?: boolean;
}


interface InfoCommandOptions {
    showKey?: boolean;
}


interface CArrayCommandOptions {
    name: string;
}

const keyBitSizes: number[] = [128, 192, 256];
const cIdentifierPattern: RegExp = /^[A-Za-z][A-Za-z0-9_]*$/;


export function registerKeyCommands(program: Command): void {
    const key: Command = program
        .command("key")
        .description("make and inspect AES key files (keep them out of version control)");

    addGenerateCommand(key);
    addKeyInfoCommand(key);
    addCArrayCommand(key);
}


function addGenerateCommand(key: Command): void {
    key
        .command("generate")
        .description("generate a random AES key and write it to a key file")
        .requiredOption("-o, --output <file.key>", "key file to write")
        .option("--bits <n>", "AES key size: 128, 192 or 256", parseKeyBits, 256)
        .option("--comment <text>", "free text stored in the key file", "")
        .option("--force", "overwrite an existing file")
        .addHelpText("after", "\nExample:\n  $ vboot key generate -o app.key --comment \"app key\"\n")
        .action(async (options: GenerateCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const log: Log = logOf(command);
                const generated: Uint8Array = generateKey(options.bits);
                writeKeyFile(options.output, generated, options.comment, options.force === true);

                log.info(`wrote ${options.bits}-bit key ${keyId(generated)} to ${options.output}`);
            });
        });
}


function addKeyInfoCommand(key: Command): void {
    key
        .command("info")
        .description("show the key size and key id of a key file")
        .argument("<file.key>", "key file")
        .option("--show-key", "also print the key itself")
        .addHelpText("after", "\nExamples:\n  $ vboot key info app.key\n  $ vboot key info app.key --show-key\n")
        .action(async (file: string, options: InfoCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                const bytes: Uint8Array = readKeyFile(file);
                const lines: string[] = [
                    `bits: ${bytes.length * 8}`,
                    `key id: ${keyId(bytes)}`,
                ];
                if (options.showKey === true) {
                    lines.push(`key: ${toHex(bytes)}`);
                }

                printLines(lines);
            });
        });
}


function addCArrayCommand(key: Command): void {
    key
        .command("c-array")
        .description("print the key as a C array for the bootloader build")
        .argument("<file.key>", "key file")
        .option("--name <identifier>", "C identifier of the array", "bootloaderKey")
        .addHelpText("after", "\nExample:\n  $ vboot key c-array app.key --name appKey > appKey.h\n")
        .action(async (file: string, options: CArrayCommandOptions, command: Command): Promise<void> => {
            await runCommand(command, async (): Promise<void> => {
                if (!cIdentifierPattern.test(options.name)) {
                    throw new VbootError(`"${options.name}" is not a valid C identifier`);
                }

                const bytes: Uint8Array = readKeyFile(file);
                printLines([formatCArray(bytes, options.name)]);
            });
        });
}


function parseKeyBits(value: string): number {
    const bits: number = parseIntegerArgument(value);
    if (!keyBitSizes.includes(bits)) {
        throw new InvalidArgumentError("key size must be 128, 192 or 256 bits");
    }

    return bits;
}
