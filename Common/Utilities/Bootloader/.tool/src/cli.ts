#!/usr/bin/env node
import { Command, Option } from "commander";
import { registerDeviceCommands } from "./commands/DeviceCommands.ts";
import { registerImageCommands } from "./commands/ImageCommands.ts";
import { registerKeyCommands } from "./commands/KeyCommands.ts";
import { registerPackageCommands } from "./commands/PackageCommands.ts";
import {
    defaultEraseTimeoutMs,
    defaultRetries,
    defaultTimeoutMs,
    defaultTransportName,
    parseIntegerArgument,
    parsePositiveIntegerArgument,
    toolVersion,
} from "./commands/CommandContext.ts";
import type { TransportFactory } from "./transport/Transport.ts";
import { createRegistry } from "./transport/transports.ts";
import type { TransportRegistry } from "./transport/TransportRegistry.ts";


const registry: TransportRegistry = createRegistry();
const transportNames: string = registry.list().map((factory: TransportFactory): string => factory.name).join(", ");

const program: Command = new Command("vboot");

program
    .description("Host tool for the VHAL bootloader: pack, encrypt and flash firmware over pluggable transports")
    .version(toolVersion())
    .option("-t, --transport <name>", `transport to talk to the device (${transportNames})`, defaultTransportName)
    .option("--timeout <ms>", "time to wait for a response to one command", parsePositiveIntegerArgument, defaultTimeoutMs)
    .option("--erase-timeout <ms>", "time to wait for an erase to finish", parsePositiveIntegerArgument, defaultEraseTimeoutMs)
    .option("--retries <n>", "how often a command packet is sent again after a timeout", parseIntegerArgument, defaultRetries)
    .addOption(new Option("-v, --verbose", "print stack traces and detailed protocol logging").conflicts("quiet"))
    .addOption(new Option("-q, --quiet", "print only results and errors").conflicts("verbose"))
    .addHelpText(
        "after",
        `
Transport options are generated from the registered transports, for example --ble-device <id>
or --sim-args=<text>. Run "vboot transports" for the full list.

Typical use:
  $ vboot key generate -o app.key
  $ vboot pack app.bin -o app.vbp --key app.key
  $ vboot -t ble flash app.vbp --start
`,
    );

addTransportOptions(program, registry);

registerDeviceCommands(program);
registerPackageCommands(program);
registerKeyCommands(program);
registerImageCommands(program);

await program.parseAsync(process.argv);


function addTransportOptions(target: Command, transports: TransportRegistry): void {
    for (const factory of transports.list()) {
        for (const option of factory.options) {
            const flags: string = `--${factory.name}-${option.name} <value>`;
            const description: string = `${factory.name} transport: ${option.description}`;
            if (option.defaultValue === undefined) {
                target.option(flags, description);
            } else {
                target.option(flags, description, option.defaultValue);
            }
        }
    }
}
