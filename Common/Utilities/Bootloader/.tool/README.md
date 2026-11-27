# vboot

Command-line host tool for the VHAL bootloader: AES key files, firmware packages (plain or encrypted), flashing over
Bluetooth LE or other pluggable transports, image conversion and factory images. A device simulator built from the
bootloader sources is included for testing without hardware.

Documentation: [`.wiki/Common/Utilities/Bootloader/.tool/README.md`](../../../../.wiki/Common/Utilities/Bootloader/.tool/README.md)
(also on the documentation site under Utilities, Bootloader, `.tool`).

## Install

Node.js 24 or newer. TypeScript runs directly in Node, there is no build step.

```
npm install
npm link            # optional: puts "vboot" on the PATH, otherwise use "node src/cli.ts"
vboot --help
```

## Quick start

```
vboot key generate -o app.key
vboot key c-array app.key --name firmwareKey       # for the bootloader build
vboot pack app.elf -o app.vbp --key app.key
vboot flash app.vbp --start
```

## Development

```
npm run check        # type check
npm run sim:build    # build the simulator (needs g++ with C++23)
npm test             # unit tests, plus end-to-end tests when the simulator is built
```

Layout: `src/protocol` (bootloader packets, client, flash procedure), `src/transport` (one folder per transport,
registered in `src/transport/transports.ts`), `src/image` and `src/crypto` (ELF, Intel HEX, packages, keys, AES),
`src/commands` (the CLI), `test` (the tests). The simulator is in the module's `.test` folder.
