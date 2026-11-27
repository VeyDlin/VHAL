import { spawnSync, type SpawnSyncReturns } from "node:child_process";
import { mkdirSync } from "node:fs";
import path from "node:path";
import process from "node:process";


// The tool folder sits in VHAL/Common/Utilities/Bootloader/.tool, so VHAL is four levels up
const toolDir: string = path.resolve(import.meta.dirname, "..");
const vhalDir: string = path.resolve(toolDir, "..", "..", "..", "..");
const simDir: string = path.resolve(toolDir, "..", ".test");
const buildDir: string = path.join(simDir, "build");
const isWindows: boolean = process.platform === "win32";
const exePath: string = path.join(buildDir, isWindows ? "vboot-sim.exe" : "vboot-sim");

const compilerArgs: string[] = [
    "-std=c++23",
    "-O1",
    "-Wall",
    "-Wextra",
    "-I",
    path.join(simDir, "shim"),
    "-I",
    path.join(vhalDir, "Common"),
    path.join(simDir, "main.cpp"),
    path.join(vhalDir, "Common", "Utilities", "Crypto", "AES", "AES.cpp"),
    "-o",
    exePath
];

// Static on Windows, so the exe does not need the compiler's DLLs on PATH
if (isWindows) {
    compilerArgs.push("-static");
} else {
    compilerArgs.push("-lpthread");
}


function buildSimulator(): number {
    mkdirSync(buildDir, { recursive: true });

    const result: SpawnSyncReturns<Buffer> = spawnSync("g++", compilerArgs, { stdio: "inherit" });

    if (result.error !== undefined) {
        console.error(`error: cannot run g++: ${result.error.message}`);
        return 1;
    }

    if (result.status !== 0) {
        return result.status ?? 1;
    }

    console.log(`built ${exePath}`);
    return 0;
}


process.exit(buildSimulator());
