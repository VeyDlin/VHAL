# vboot -- Bootloader Host Tool

`vboot` is the command-line host tool for the [Bootloader](/docs/Common/Utilities/Bootloader). It generates AES keys, packs firmware images into packages (optionally encrypted), and flashes them to the device over a pluggable transport. It also converts ELF and Intel HEX files and builds factory images that hold the bootloader and the application together.

The tool lives in `Common/Utilities/Bootloader/.tool`. It is plain TypeScript that Node.js runs directly, so there is no build step.

## Table of Contents

- [Install](#install)
- [Quick Start](#quick-start)
- [Encrypted Images Step by Step](#encrypted-images-step-by-step)
- [Plain Images](#plain-images)
- [What flash Does](#what-flash-does)
- [Commands](#commands)
- [Global Options](#global-options)
- [Transports](#transports)
- [Simulator](#simulator)
- [Package Format](#package-format)
- [Retrying after a Lost Response](#retrying-after-a-lost-response)
- [Security Notes](#security-notes)
- [Troubleshooting](#troubleshooting)

## Install

Requirements: Node.js 24 or newer. The Bluetooth transport also needs a Bluetooth LE adapter. Its native module (`@stoprocent/noble`) is an optional dependency, so every other command works without it.

```
cd VHAL/Common/Utilities/Bootloader/.tool
npm install
npm link            # optional: puts "vboot" on the PATH
vboot --help
```

Without `npm link`, run `node src/cli.ts` in the tool folder instead of `vboot`. Every example below uses `vboot`.

`vboot <command> --help` shows the options and examples of a command.

## Quick Start

```
vboot key generate -o app.key                              # once per product: a random AES-256 key
vboot key c-array app.key --name firmwareKey               # the key as a C array for the bootloader build
vboot pack app.elf -o app.vbp --key app.key                # encrypted package, AES-256-CTR, random IV
vboot scan                                                 # devices that advertise the bootloader service
vboot flash app.vbp --start                                # erase, write, check, finalize, verify, start
```

## Encrypted Images Step by Step

**1. Make a key.** One key per product. `*.key` files are git-ignored in the tool folder; keep them out of every repository.

```
vboot key generate -o app.key                 # --bits 128|192|256, default 256
vboot key info app.key                        # size and key id; the key itself only with --show-key
```

**2. Put the key into the bootloader build.** `key c-array` prints the key as a C++ array:

```
vboot key c-array app.key --name firmwareKey > BootloaderKey.h
```

```cpp
#include "BootloaderKey.h"     // git-ignored, generated from app.key

class MyBootloader : public AESFLASHBootloader<AESKeySize::AES256> {
public:
    MyBootloader(ICommunication& comm, AFLASH& flash)
        : AESFLASHBootloader(comm, flash, AESMode::CTR, firmwareKey, 0x08020000, 0x60000, defaultIv) {}
    // ...
};
```

The key size of the bootloader (`AES128`/`AES192`/`AES256`) and its mode must match the package. The constructor IV is only a default: `flash` sends a fresh IV with every package (`SetIV`).

**3. Pack the application.**

```
vboot pack app.elf -o app.vbp --key app.key                          # cipher aes-<key bits>-ctr
vboot pack app.bin -o app.vbp --key app.key --base 0x08020000         # a raw binary needs its address
vboot pack app.elf -o app.vbp --key app.key --cipher aes-256-cbc --label "1.4.0"
vboot inspect app.vbp                                                # --json for the raw manifest
```

The package records the cipher, a random IV, the key id (not the key), the address the image was linked for, and the CRC-32 and size of the image as it will be stored in flash.

**4. Flash it.**

```
vboot flash app.vbp --start                    # the only device that advertises the service
vboot flash app.vbp --ble-device MyDevice --reset
```

## Plain Images

A device built on `FLASHBootloader` (no encryption) takes the image as it is:

```
vboot flash app.elf --start
vboot flash app.bin --base 0x08020000 --start
vboot pack app.elf -o app.vbp                  # a package without --key is plain too
```

## What flash Does

1. `Ping`, then `GetInfo`. It prints the region, the largest packet size and the cipher of the device.
2. It checks the package against the device. These stop the command:
   - the device reports a cipher different from the package (a plain image to an AES device, CBC to a CTR device, ...);
   - the image was linked for another address than the region start;
   - the image does not fit before the 8-byte firmware header.
   A device that reports no cipher (an older bootloader) only produces a warning.
3. `SetIV` with the package IV (CBC and CTR only).
4. `Erase`. The device answers when the erase is done (`--erase-timeout`, default 60 s).
5. `Write` in chunks of the device's packet data size (240 by default), rounded down to a multiple of 16 bytes. A progress line shows the bytes sent. A `--chunk` value is rounded down the same way and checked before anything on the device changes.
6. `GetHash`. The SHA-256 of the bytes the device received must equal the hash of the payload.
7. `Finalize` with the expected CRC-32 and size. The device checks the decrypted flash content against them. A wrong key or IV gives `ChecksumMismatch`, and the header is not written, so the old image stays invalid and the device stays in the bootloader.
8. `Verify`.
9. `Start` or `Reset` when `--start` or `--reset` is given.

## Commands

### Device

Each device command opens the transport, runs, and closes it again.

| Command | What it does |
|-|-|
| `scan [--duration <ms>]` | Lists the devices the transport can see (BLE: devices that advertise the bootloader service) |
| `ping` | Checks that the bootloader answers |
| `info` | Bootloader version, memory start and size, read-out protection level, access state, transport packet size, packet data size, cipher |
| `status` | Device state and the number of bytes received for the current image |
| `hash` | SHA-256 of the raw data received for the current image |
| `flash <file> [--start] [--reset] [--chunk <n>] [--base <addr>]` | Full update (see [What flash Does](#what-flash-does)). `<file>` is a `.vbp` package or a plain image (see [Image files](#image-files)) |
| `erase` | Erases the application region; a new image starts |
| `verify` | Asks whether the image in flash is valid; exits with an error when it is not |
| `start` | Starts the application (the device answers, then jumps about 150 ms later). If that answer is lost, the retries find no bootloader and the command times out, although the application may already run |
| `reset` | Resets the device (it answers, then reboots) |
| `read -o <file> [--offset <n>] [--length <n>]` | Reads back the image written since the device started. Refused in AES mode, because the flash holds plaintext the host should not get back |
| `unlock --password <hex>` / `--password-file <file>` | Removes read-out protection (`UnlockDevice`); on STM32 this erases the flash |
| `user-data get <type>` / `user-data set <type> <hex>` | Reads or writes a user data entry (`UserData`, type 0-255), when the device has a handler. A command body (2 bytes plus the data) may not exceed the device's packet data size (240 by default); the tool refuses a longer one before sending |
| `set-iv <hex>` | Sends `SetIV` by hand; `flash` does this by itself |

### Packages

| Command | What it does |
|-|-|
| `pack <image> -o <file.vbp> [--key <file>] [--cipher <name>] [--iv <hex>] [--base <addr>] [--label <text>]` | Makes a package. The cipher is `none` without `--key` and `aes-<key bits>-ctr` with it |
| `inspect <file.vbp> [--json]` | Shows the manifest; checks the magic, the version, the sizes and the payload hash |

Cipher names: `none`, `aes-128-ecb`, `aes-128-cbc`, `aes-128-ctr`, and the same for 192 and 256 bits. The key size must match the cipher. CBC and CTR take an IV: without `--iv` a random one is generated. ECB takes none. ECB and CBC pad the image with `0xFF` to a multiple of 16 bytes; CTR does not pad.

### Keys

| Command | What it does |
|-|-|
| `key generate -o <file.key> [--bits 128\|192\|256] [--comment <text>] [--force]` | Writes a random key file; refuses to overwrite without `--force` |
| `key info <file.key> [--show-key]` | Key size and key id (the first 8 bytes of SHA-256 of the key, in hex) |
| `key c-array <file.key> [--name <identifier>]` | The key as a C array for the bootloader build |

### Images

| Command | What it does |
|-|-|
| `image <file> -o <file.bin\|file.hex> [--base <addr>]` | Converts ELF or Intel HEX (or a raw binary with `--base`) to a raw binary or Intel HEX. Prints the base address and the size |
| `merge --bootloader <file> --application <file> --region-start <addr> --region-size <bytes> -o <file> [--bootloader-base <addr>] [--application-base <addr>]` | Builds the factory image (below) |

### Image files

Wherever an image is read (`flash`, `pack`, `image`, `merge`), the format is detected by content: a file that starts with the ELF magic is read as ELF (32-bit little-endian, the load addresses of the segments), a text file that starts with `:` records is read as Intel HEX, and anything else is a raw binary, which needs its address from `--base` (or `--bootloader-base`/`--application-base`). A file named `.elf` or `.hex` whose content is not that format is an error. Gaps between segments are filled with `0xFF`; an image that spans more than 64 MiB is refused.

Numbers and addresses are decimal or `0x` hexadecimal. A number with a leading zero such as `08020000` is refused as ambiguous: write `0x08020000`.

### Factory images

`merge` places the bootloader at its address and the application at `--region-start`, fills the rest with `0xFF`, and writes the 8-byte firmware header (CRC-32 and size of the application) into the last 8 bytes of the region. A device programmed with that image starts the application without an upload. The application must be linked at the region start and end before the header. The output is `.bin` (starting at the bootloader address) or `.hex`.

## Global Options

| Option | Default | Meaning |
|-|-|-|
| `-t, --transport <name>` | `ble` | Transport to the device (`vboot transports` lists them) |
| `--timeout <ms>` | 3000 | Time to wait for the response to one command |
| `--erase-timeout <ms>` | 60000 | Time to wait for `Erase`, and for `Finalize` and `Verify`, which check the whole image |
| `--retries <n>` | 3 | How often a command is sent again after a timeout |
| `-v, --verbose` | | Protocol logging, and stack traces on errors |
| `-q, --quiet` | | Results and errors only |

Every transport adds its options as `--<transport>-<option> <value>` (see [Transports](#transports)). A value that starts with `-` must be passed with `=`, for example `--sim-args="--cipher aes-256-ctr --key <hex>"`.

Errors are printed as `error: <message>` and the exit code is 1.

## Transports

| Transport | Options | Notes |
|-|-|-|
| `ble` | `--ble-device <name or address>`, `--ble-scan-timeout <ms>` (5000), `--ble-connect-timeout <ms>` (10000) | Bluetooth LE to the [NRF8001Communication](/docs/Common/Utilities/Bootloader) service: service `4242`, RX `4243` (write), TX `4244` (notify). Without `--ble-device` the only device that advertises the service is used. A name that several boards share is an error; an address always selects one board. The connect timeout applies to the connection, the service discovery and the subscription, each |
| `sim` | `--sim-exe <path>`, `--sim-args <text>` | The [simulator](#simulator) |

Both carry the bootloader packets in the `CompactStreamingProtocol` fragment format v2 (see [Fragment Format (v2)](/docs/Common/Utilities/Bootloader#fragment-format-v2)).

### Adding a transport

A transport is a folder under `src/transport/` that exports a `TransportFactory`, plus one line in `src/transport/transports.ts`. Its options appear on the command line by themselves.

```ts
export interface TransportFactory {
    readonly name: string;                    // value of --transport
    readonly description: string;
    readonly options: TransportOption[];      // become --<name>-<option> <value>
    create(options: Map<string, string>, log: Log): Transport;
    scan?(options: Map<string, string>, durationMs: number, log: Log): Promise<DiscoveredDevice[]>;
}

export interface Transport {
    readonly name: string;
    open(): Promise<void>;
    close(): Promise<void>;
    send(message: Uint8Array): Promise<void>;                     // resolves when the message is delivered
    setDataListener(listener: (data: Uint8Array) => void): void;   // received bytes, in order
    setResetListener(listener: () => void): void;                  // framing lost (incomplete message, reconnect)
}
```

A transport that moves small packets over a link without its own framing (BLE, UART) only implements a `PacketLink` (`open`, `close`, `write(packet)`, a packet listener, a closed listener and `maxPacketSize`) and wraps it in `CspTransport`, which adds the fragment protocol. A stream that already delivers whole messages reliably can implement `Transport` directly.

## Simulator

The simulator runs the real bootloader code (`FLASHBootloader` or `AESFLASHBootloader`, `CompactStreamingProtocol`) against an in-memory flash, with the transport on its stdin and stdout. It needs `g++` with C++23 on the PATH.

```
npm run sim:build                     # writes .test/build/vboot-sim(.exe)
vboot -t sim info
vboot -t sim flash app.elf
vboot -t sim --sim-args="--cipher aes-256-ctr --key <hex> --flash-out flash.bin" flash app.vbp --reset
```

Simulator arguments (inside `--sim-args`):

| Argument | Meaning |
|-|-|
| `--cipher none\|aes-<128\|192\|256>-<ecb\|cbc\|ctr>` | Bootloader type (default `none`) |
| `--key <hex>`, `--iv <hex>` | AES key and constructor IV |
| `--region-start <n>`, `--region-size <n>` | Application region (default `0x08020000`, `0x40000`) |
| `--alignment <n>` | Write alignment (plain bootloader only) |
| `--flash-in <file>`, `--flash-out <file>` | Load the region before start; write it when the simulator exits (end of the command, `Start`, `Reset`). A failed write makes the simulator exit with code 1 |
| `--rdp 0\|1\|2`, `--allow-updates-when-read-protected` | Read-out protection and the update policy |
| `--strict-program` | Refuse to program bytes that are not erased (like STM32G0/G4) |
| `--user-data` | A `UserData` handler that stores the entries in memory |

The simulator is started for each command, so state on the device (the written image, user data) lasts one command. Use `--flash-out`/`--flash-in` to carry the flash content between commands.

`npm test` runs the unit tests. `npm run test:e2e` runs the end-to-end tests against the simulator. The repository's test run (`./test.cmd Bootloader`) builds the simulator with `npm run sim:build` first when a g++ is on the PATH, and the end-to-end entry (`Bootloader.tool.e2e`) fails when that build fails. Without a g++ the entry is skipped.

## Package Format

A `.vbp` file:

```
offset 0    "VBPK"                      4 ASCII bytes
       4    u16 format version = 1
       6    u16 reserved = 0
       8    u32 manifest length N
      12    manifest                    N bytes, UTF-8 JSON
    12+N    payload                     the bytes sent with Write, nothing after it
```

All integers are little-endian. Manifest fields:

| Field | Meaning |
|-|-|
| `cipher` | `none` or `aes-<bits>-<mode>` |
| `iv` | Hex, 16 bytes; `null` for `none` and ECB |
| `keyId` | First 8 bytes of SHA-256 of the key, hex; `null` for `none` |
| `baseAddress` | Address the image was linked for, or `null` |
| `image` | `size` and `crc32` of the image as stored in flash (after padding, before encryption): the values `Finalize` sends |
| `payload` | `size` and `sha256` of the payload: the value `GetHash` must return |
| `label`, `created`, `tool` | Free text, ISO 8601 time, tool version |

## Retrying after a Lost Response

Each command keeps its sequence number while it is repeated after a timeout (`--retries`), so the device can recognise the repeat:

| Command | Repeat |
|-|-|
| `Write` | The device answers the stored status and does not write the data twice |
| `Read` | The tool uses the offset form, which does not move the read position |
| `Finalize` | The device answers `Success` without rewriting an identical header |
| `Erase` | A repeated erase starts again; the upload has not begun yet at that point |
| `Ping`, `GetInfo`, `GetStatus`, `GetHash`, `Verify`, `SetIV` | Idempotent |

The fragment layer below retransmits lost fragments by itself, so a command is repeated only when a whole exchange is lost.

## Security Notes

- A key file holds the key in plain text (JSON, hex). It is created with owner-only permissions where the OS supports them. Never commit it; the tool folder ignores `*.key` and `*.vbp`.
- A package carries the key id, never the key. The IV is not secret, but it must be new for every image: two CTR images with the same key and IV share a keystream. `pack` generates a random IV unless `--iv` is given.
- The AES modes hide the image but do not authenticate it. The CRC-32 in `Finalize` detects a wrong key or a damaged upload; it is not a signature. Anyone who can reach the device can erase it or write an image, unless the bootloader restricts that (read-out protection with the default `allowUpdatesWhenReadProtected = false`, or the device's own checks).
- While read-out protection is active, `Erase`, `Write`, `Finalize` and `SetIV` answer `Protected` by default, so `flash` fails until the device is unlocked.

## Troubleshooting

| Message | Cause |
|-|-|
| `the image in flash does not match the package (wrong key or IV on the device?)` | `Finalize` answered `ChecksumMismatch`. The bootloader decrypts with another key than the package was made with, or another key size or mode |
| `the device is configured for aes-ctr but the image is plain` (or another pair) | The package and the bootloader use different ciphers. Pack again with the matching `--cipher` and key |
| `the image was linked for 0x..., the device region starts at 0x...` | The application was linked for another address. Fix the linker script, or pass the right `--base` for a raw binary |
| A device error `Protected` | Read-out protection is active; see [Unlocking Read-Out Protection](/docs/Common/Utilities/Bootloader#unlocking-read-out-protection) |
| `no device advertising the bootloader service was found` | The device is not in bootloader mode, is out of range, or another host is connected to it |
| `N devices advertise the bootloader service, choose one with --ble-device` | Pass the name or address of the one to update (`vboot scan` lists them) |
| The BLE transport fails to load | The optional native module is missing: run `npm install` in the tool folder on the machine that uses it |
