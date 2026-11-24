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
`cfgmgr32`, `hid` and `winmm` on Windows, `libusb-1.0` on Linux. Build in a folder of your own:

```sh
cmake -G Ninja -DCMAKE_CXX_COMPILER=g++ -B build/vubcheck Common/Drivers/Interface/VUB/.tool/board
cmake --build build/vubcheck
```

On Windows the result is `build/vubcheck/vubcheck.exe`. It is linked statically, so it imports only Windows system DLLs (HID, SETUPAPI,
WINUSB, WINMM, KERNEL32 and MSVCRT). The build has no warnings with `-Wall -Wextra`.

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
| `--count N` | 200 | payloads to send, 1 to 65536 |
| `--seed N` | from the random device | seed of the payload sizes and contents, 0 to 4294967295; printed in the summary, so a run can be repeated |
| `--loss K` | 0 | on the PC side, drop every K-th outgoing frame (serial and bulk) or packet (HID); a dropped frame is not sent, so the board's delivery layer must resend it. K is 0 or more |
| `--inflight N` | 4 | payloads sent and not yet echoed at once, 1 to 4; a larger limit is refused, see Pacing |
| `--timeout S` | 120 | seconds to wait for every echo and every outcome, 1 to 86400 (one day) |

Every number is decimal digits only. A sign, a space, other text or a value outside the range is a usage error: the program prints the
usage and exits with 2, before it opens any port or device.

A payload is 3 to 32 bytes: two bytes of number, then random bytes. The sizes and the contents come from the seed.

`--loss` needs a value of 2 or more to leave some frames through. With a stream link the program keeps the bytes of a frame until the frame
is whole, then drops or sends it, so the framing on the wire stays intact. The frame ends are the COBS zero byte for `uart` and `cdc`, and
the header of `LengthPrefix<2>` for `bulk`. A dropped frame is counted in `dropped`.

## Pacing

A run keeps at most `--inflight` payloads in flight. A payload is in flight from its send until its echo is checked. The default, 4, is
the delivery layer's window of the PC chain (`Window(4)`), and it is the number of echoes the board can keep.

The limit protects the board's echo queue. The board keeps each payload it receives in its echo queue, which has four slots
(`EchoSlots` in `.demo/STM32G4/Application/DemoVub/DemoVub.cpp`), until the echo goes to its delivery layer. The delivery layer limits the
packets it has not yet acknowledged, not the payloads that have not yet been echoed. Without the limit, the PC sends as fast as the window
takes payloads, so the board receives payloads faster than it echoes them. With `--loss`, some of the board's echoes are not acknowledged
and the board resends them, so its echo sends wait, its queue fills, and it drops payloads (`echoDropped`). The run then fails on the
echo queue, and the delivery layer's resends are not what it checks. With the limit at 4, at most four payloads wait in the board's queue,
so a run with `--loss` checks the delivery layer's resends. The program refuses a larger value, so it never overfills the queue.

## Timing

The run loop sleeps 1 ms between rounds, and each round sends at most one payload. On Windows a sleep lasts the system timer period, 15.6 ms
unless a process asks for a finer one, which would hold the loop to about 64 rounds a second. The run asks for a 1 ms period with
`timeBeginPeriod(1)` when it starts and gives it back with `timeEndPeriod(1)` when it ends (the program links `winmm`).

The `rate` of the summary is the echoes per second over the whole run, from the start of the loop to its end. It is measured on the PC, so it
includes the loop's pace: with the 1 ms period the loop sends at most about 1000 payloads a second, and the rate is at most that. The rate
shows the speed of this run through this loop and this link. It does not show the capacity of the link.

## The summary

Each run ends with one line on standard output and an exit code:

The line has this shape, with the values of the run in place of the angle brackets:

```
vubcheck link=<link> sent=<n> echoed=<n> ok=<n> retried=<n> dropped=<n> mismatched=<n> duplicate=<n> unknown=<n> restarts=<n> rate=<n>/s seconds=<s> inflight=<n> seed=<seed> result=<PASS|FAIL>
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
| `rate` | echoes per second over the run, measured on the PC; see Timing |
| `seconds` | the length of the run |
| `inflight` | the in-flight limit of the run; see Pacing |
| `seed` | the seed of the run |
| `result` | `PASS` or `FAIL` |

Exit codes: 0 for `PASS`, 1 for `FAIL` (an echo missing, wrong or repeated, an outcome not ok, or a refused payload), 2 for a usage error or a
port that does not open. A `FAIL` run prints its counts on the same line, so the line says which check failed.

When echoes are missing (`echoed` is below `sent`) the failed run prints a second line, `board:`, with the board-side meaning. A missing echo
is either an echo the board dropped, because its echo queue was full (`echoDropped`), or an echo it gave up on, because no acknowledgement
came after the last resend on its side (`outcomesFailed`, with `retryExhausted`). A payload the board never received leaves no board counter,
so then the `ok` of the PC is below `sent`. The counters are read as described in What the board shows.

The expected result of a run without `--loss` is `sent=N echoed=N ok=N` with `retried=0`, every other count at zero, and `result=PASS`.
A run with `--loss 5` is expected to pass with `dropped` above zero. `retried` counts the payloads this side sent again, so it is above
zero when a dropped payload frame had to be resent. A dropped acknowledgement is a frame of this side too: the board then resends its echo,
the PC acknowledges that copy and does not deliver it twice. Neither side counts that resend: the PC did not send it, and the board's
status block has no resend counter.

## What the board shows

The board's status block is a volatile structure named `vubStatus` (`.demo/STM32G4/Application/DemoVub/Status/VubStatus.h`), read over SWD.
Its `stage` is 5 when the stacks are serving. `usbResult` and `serialResult` are the results of the USB start and the USART1 setup. The
`sessionSource` is 1 when the session values came from the RNG, and 2 when the fallback did. Each stack has its counters: `received`,
`echoed`, `echoRefused`, `echoDropped`, `outcomesOk`, `outcomesFailed`, `restarts`, the last statuses and the drop counters. A critical
error, when one is raised with `System::CriticalError`, is kept in `criticalMessage`, `criticalFile` and `criticalLine`.

### Reading the counters over SWD

The block is in RAM, so it is read while the board runs, and a reset clears it: read it before the board is reset. The address comes from
the ELF of the flashed build:

```sh
arm-none-eabi-nm build/RelSpeed-Vub/VubG431.elf | grep vubStatus
STM32_Programmer_CLI -c port=SWD -r32 0x<address> 86
```

The block is 344 bytes, which is 86 words, so `86` reads all of it. Use the address `nm` printed, and the path of the CLI from
`STM32_PROGRAMMER_CLI` when it is not on the PATH. Add `sn=<serial>` (`STM32_SWD_SN`) when more than one ST-LINK is connected. The words
are in the order of the struct, so the byte offset of a field is 4 times its word index:

| Offset | Field |
|---|---|
| 0 | `magic` |
| 4 | `stage` |
| 8, 12, 16, 20 | `consoleResult`, `serialResult`, `sessionSource`, `usbResult` |
| 24 | `uartErrors`: parity, noise, frame and overrun, 4 bytes each |
| 40 | the `uart` stack, 72 bytes |
| 112 | the `cdc` stack, 72 bytes |
| 184 | the `bulk` stack, 72 bytes |
| 256 | the `hid` stack, 72 bytes |
| 328 | `criticalSet`, then `criticalMessage`, `criticalFile` and `criticalLine` |

Each stack starts with its counters, in this order, at 4 bytes each: `session` (0), `received` (4), `echoed` (8), `echoRefused` (12),
`echoDropped` (16), `outcomesOk` (20), `outcomesFailed` (24), `restarts` (28), `lastSendStatus` (32) and `lastOutcomeStatus` (36). The eight
drop counters follow from 40: `ringFull`, `queueFull`, `tooLong`, `framingError`, `transformError`, `layerError`, `sendError` and `linkDown`.

| Counter | Meaning |
|---|---|
| `received` | payloads the stack's delivery layer passed up, that is, payloads the PC sent and the layer acknowledged |
| `echoed` | echoes the stack's delivery layer accepted for sending; this is not an acknowledgement |
| `echoRefused` | echoes the layer refused with a status other than busy; the echo is dropped |
| `echoDropped` | payloads the echo queue could not keep, because its four slots were full; the layer had acknowledged them, so the PC does not resend them |
| `outcomesOk` | echoes the PC acknowledged |
| `outcomesFailed` | echoes the board gave up on: `retryExhausted` (no acknowledgement after the last resend) or `reset` |
| `restarts` | sessions the board started again, because the PC lost the session |
| `lastSendStatus`, `lastOutcomeStatus` | the `ResultStatus` values of the last send and the last outcome |
| drop counters | frames or packets the stack dropped, by reason |

After a run that has nothing left in the echo queue, `received` equals `echoed` plus `echoRefused` plus `echoDropped`.

To read a failed run from the board: a non-zero `echoDropped` with the default `--inflight` points at the board's queue, and a non-zero
`outcomesFailed` means the board gave up on echoes it sent. Each run's counters are the counters since the board's last reset, so a second
run needs the counters of the first subtracted, or a reset before it.

## Results

Measured on 2026-11-24 on an STM32G431 with the RelSpeed image of this demo (112232 bytes of text, 14116 bytes of data and bss), and
vubcheck built with MinGW g++ 15.2 on Windows 11. USART1 goes through a CH343 adapter (COM3); the CDC port was COM13. Each line is one
vubcheck run; each run is a new PC stack against the board's running stack, so every run after the first on a link also checks a restart
of the PC side.

| Run | Sent | Echoed | ok | Retried | Dropped | Rate | Result |
|---|---:|---:|---:|---:|---:|---:|---|
| `uart`, twice | 200 | 200 | 200 | 0 | 0 | 256 per second | PASS |
| `cdc`, twice | 200 | 200 | 200 | 0 | 0 | 263 to 267 per second | PASS |
| `bulk`, twice | 200 | 200 | 200 | 0 | 0 | 263 to 268 per second | PASS |
| `uart --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 258 per second | PASS |
| `cdc --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 267 per second | PASS |
| `bulk --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 270 per second | PASS |
| `uart --loss 5` | 200 | 200 | 200 | 51 | 100 | 19 per second | PASS |
| `cdc --loss 5` | 200 | 200 | 200 | 48 | 100 | 19 per second | PASS |
| `bulk --loss 5` | 200 | 200 | 200 | 40 | 100 | 17 per second | PASS |
| `bulk --loss 3 --count 500` | 500 | 498 | 498 | 375 | 498 | 4 per second | FAIL |

With `--loss 3` one frame in three of the PC side is dropped, acknowledgements included, and two payloads lost all four attempts
(`Retries(3)`): their outcome is `retryExhausted`, which the delivery layer reports for a packet it gave up on. A run at that loss rate
needs more retries. The rate is limited by the PC loop and the window of 4 payloads in flight, not by the links. The `hid` check was
not run: it opens the board's HID device on the PC, and waits for the owner's go-ahead.

## Notes

- The session values differ on every boot when `sessionSource` is 1: the RNG gives four words, and each stack takes the low 16 bits
  of one of them. The fallback (2) can repeat across boots, so a check that reboots the board needs the RNG source.
- The status block is refreshed every 100 ms, so its counters can lag the last echo by that much.
- A board that is not flashed with this demo has no `1209:0007` device and no COM port for the bulk and HID checks.
