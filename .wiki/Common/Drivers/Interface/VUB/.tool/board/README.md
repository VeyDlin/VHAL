# VUB board check

The board check runs VUB on the real link of the demo board `.demo/STM32G4` (`VHAL_DEMO=Vub`, see its README): a PC program,
`vubcheck`, talks to the board's four stacks and checks every echo. The board is an STM32G431 on USB and USART1. It echoes each payload
its stack takes back to the sender, so a run checks the link in both directions, the framing, the delivery layer's acknowledgements and
resends, and the outcomes the delivery layer reports.

The program is in `Common/Drivers/Interface/VUB/.tool/board/`: `CMakeLists.txt` and `src/`. It uses the PC links of `VUB::PC` and the
same chains as the device, with `VUB::Reliable` and a payload of at most 32 bytes. It is not run by any test: it opens a serial port, a
USB interface or a HID device, so a run is done by hand, against a board that is flashed.

## Build

The program needs CMake and a C++20 compiler: MinGW g++ on Windows, g++ on Linux. It links only system libraries: `winusb`, `setupapi`,
`cfgmgr32` and `hid` on Windows, `libusb-1.0` on Linux. Build in a folder of your own:

```sh
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -B build/vubcheck Common/Drivers/Interface/VUB/.tool/board
cmake --build build/vubcheck
```

On Windows the result is `build/vubcheck/vubcheck.exe`. It is linked statically, so it imports only Windows system DLLs (HID, SETUPAPI,
WINUSB, KERNEL32 and MSVCRT). The build has no warnings with `-Wall -Wextra`.

## Flash

The board is flashed over SWD with STM32CubeProgrammer, by the script of the demo folder:

```sh
cmake --preset RelSpeed -B build/RelSpeed-Vub -DVHAL_DEMO=Vub
cmake --build build/RelSpeed-Vub
sh .demo/STM32G4/flash.sh build/RelSpeed-Vub/VubG431.bin
```

The script verifies the image and resets the board. Set `STM32_PROGRAMMER_CLI` when the CLI is not on the PATH, and `STM32_SWD_SN` when
more than one ST-LINK is connected.

## The four checks

Each run opens one link of the board, starts its stack, sends payloads and waits for every echo. Each payload starts with its number, so an
echo is matched with its payload whatever order the echoes come back in.

```sh
vubcheck --link uart --port COM3
vubcheck --link cdc --port COMn
vubcheck --link bulk
vubcheck --link hid
```

| Link | Board side | PC side | What the run proves |
|---|---|---|---|
| `uart` | USART1 (PA9, PA10) at 115200 8N1, UART link, COBS, CRC-16/MODBUS, Reliable | `SerialLink` on the port, the same chain | the UART link in both directions; the CRC and COBS checks; the delivery layer's acknowledgement and resend over a stream |
| `cdc` | CDC-ACM serial port, CdcLink, COBS, Reliable | `SerialLink` on the COM port Windows gives the CDC interface | the USB serial link; the DTR state; the same framing as the UART without the CRC |
| `bulk` | Vendor bulk interface (WinUSB), BulkLink, `LengthPrefix<2>`, Reliable | `BulkLink`, found by the interface GUID in `DemoProducts.h` | the bulk transfers in both directions, and the length prefix over a stream of transfers |
| `hid` | Vendor HID interface (usage page `0xFF00`), HidLink, no framing, Reliable | `HidLink`, found by VID `1209`, PID `0007` and the usage page | a packet link: one report is one packet, and the resend of a report the host did not get |

`--port` is needed for `uart` and `cdc` only. For `uart` the port is COM3, the adapter of USART1. For `cdc` it is the COM port Windows
creates for the board's serial interface. Both ports are opened at 115200 8N1; the CDC link does not use the baud rate.

The `hid` run opens the board's HID device on the PC. Run it only after the owner of that PC has agreed to the device being opened.

## Options

| Option | Default | Meaning |
|---|---|---|
| `--link uart\|cdc\|bulk\|hid` | required | the link to check |
| `--port COMn` | required for `uart` and `cdc` | the serial port |
| `--count N` | 200 | payloads to send |
| `--seed N` | from the random device | seed of the payload sizes and contents; printed in the summary, so a run can be repeated |
| `--loss K` | 0 | on the PC side, drop every K-th outgoing frame (serial and bulk) or packet (HID); a dropped frame is not sent, so the board's delivery layer must resend it |
| `--timeout S` | 120 | seconds to wait for every echo and every outcome |

A payload is 3 to 32 bytes: two bytes of number, then random bytes. The sizes and the contents come from the seed.

`--loss` needs a value of 2 or more to leave some frames through. With a stream link the program keeps the bytes of a frame until the frame
is whole, then drops or sends it, so the framing on the wire stays intact. The frame ends are the COBS zero byte for `uart` and `cdc`, and
the header of `LengthPrefix<2>` for `bulk`. A dropped frame is counted in `dropped`.

## The summary

Each run ends with one line on standard output and an exit code:

The line has this shape, with the values of the run in place of the angle brackets:

```
vubcheck link=<link> sent=<n> echoed=<n> ok=<n> retried=<n> dropped=<n> mismatched=<n> duplicate=<n> unknown=<n> restarts=<n> rate=<n>/s seconds=<s> seed=<seed> result=<PASS|FAIL>
```

| Field | Meaning |
|---|---|
| `sent` | payloads handed to the stack |
| `echoed` | payloads that came back with the same number and bytes |
| `ok` | delivery outcomes reported as ok (the acknowledgement arrived) |
| `retried` | outcomes that needed more than one attempt, which is the resends the delivery layer made |
| `dropped` | frames or packets the `--loss` option dropped on the PC side |
| `mismatched`, `duplicate`, `unknown` | echoes whose bytes are wrong, echoes that came back twice, and echoes whose number is not a payload of the run; all zero on a pass |
| `restarts` | times the peer lost the session, so the delivery layer started a new one |
| `rate` | echoes per second over the run |
| `seconds` | the length of the run |
| `seed` | the seed of the run |
| `result` | `PASS` or `FAIL` |

Exit codes: 0 for `PASS`, 1 for `FAIL` (an echo missing, wrong or repeated, an outcome not ok, or a refused payload), 2 for a usage error or a
port that does not open. A `FAIL` run prints its counts on the same line, so the line says which check failed.

The expected result of a run without `--loss` is `sent=N echoed=N ok=N` with `retried=0`, every other count at zero, and `result=PASS`.
A run with `--loss 5` is expected to pass with `dropped` above zero. `retried` counts the payloads this side sent again, so it is above
zero when a dropped payload frame had to be resent. A dropped acknowledgement is a frame of this side too: the board then resends its echo,
the PC acknowledges that copy and does not deliver it twice, and the board counts the resend, not this run.

## What the board shows

The board's status block is a volatile structure named `vubStatus` (`.demo/STM32G4/Application/DemoVub/Status/VubStatus.h`), read over SWD.
Its `stage` is 5 when the stacks are serving. `usbResult` and `serialResult` are the results of the USB start and the USART1 setup. The
`sessionSource` is 1 when the session values came from the RNG, and 2 when the fallback did. Each stack has its counters: `received`,
`echoed`, `echoRefused`, `echoDropped`, `outcomesOk`, `outcomesFailed`, `restarts`, the last statuses and the drop counters. A critical
error, when one is raised with `System::CriticalError`, is kept in `criticalMessage`, `criticalFile` and `criticalLine`.

## Notes

- The session values differ on every boot when `sessionSource` is 1: the RNG gives four words, and each stack takes the low 16 bits
  of one of them. The fallback (2) can repeat across boots, so a check that reboots the board needs the RNG source.
- The status block is refreshed every 100 ms, so its counters can lag the last echo by that much.
- A board that is not flashed with this demo has no `1209:0007` device and no COM port for the bulk and HID checks.
