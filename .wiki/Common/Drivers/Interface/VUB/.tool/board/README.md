# VUB board check

The board check runs VUB on the real link of the demo board `.demo/STM32G4` (`VHAL_DEMO=Vub`, see its README): a PC program,
`vubcheck`, talks to the board's four stacks and checks every echo. The board is an STM32G431 on USB and USART1. It echoes each payload
its stack takes back to the sender, so a run checks the link in both directions, the framing, the delivery layer's acknowledgements and
resends, and the outcomes the delivery layer reports.

The program is in `Common/Drivers/Interface/VUB/.tool/board/`: `CMakeLists.txt` and `src/`. It uses the PC links of `VUB::PC` and the
same chains as the device, with `VUB::Reliable` and a payload of at most 32 bytes. The check itself is not run by any test: it opens a serial
port, a USB interface or a HID device, so a run is done by hand, against a board that is flashed. Two offline tests of the folder run with
`./test.cmd VUB -L tool`: the parser of the number options (`test/TestDecimalArgument.cpp`), and the host loopback in its echo and requests
modes at loss 0 with seeds 1 to 3 (`test/TestVubBoardLoopback.cpp`, see below).

## Build

The program needs CMake and a C++23 compiler: MinGW g++ on Windows, g++ on Linux. It links only system libraries: `winusb`, `setupapi`,
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
| `--count N` | 200 | payloads to send (requests with `--requests`), 1 to 65536 |
| `--seed N` | from the random device | seed of the payload sizes and contents, 0 to 4294967295; printed in the summary, so a run can be repeated |
| `--loss K` | 0 | on the PC side, drop every K-th outgoing frame (serial and bulk) or packet (HID), except a frame that would be the fourth loss of its packet or acknowledgement (see The loss pattern); a dropped frame is not sent, so the board's delivery layer must resend it. K is 0 or more |
| `--inflight N` | 4 | payloads sent and not yet echoed at once, 1 to 4; with `--requests`, requests open at once, 1 to 8; a larger limit is refused, see Pacing |
| `--timeout S` | 120 | seconds to wait for every echo and every outcome (every final with `--requests`), 1 to 86400 (one day) |
| `--requests` | off | the requests check of the next section instead of the echo check; only `uart` and `cdc` have it |

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
vubcheck link=<link> sent=<n> echoed=<n> ok=<n> failed=<n> retried=<n> dropped=<n> mismatched=<n> duplicate=<n> unknown=<n> restarts=<n> rate=<n>/s seconds=<s> inflight=<n> seed=<seed> result=<PASS|FAIL>
```

| Field | Meaning |
|---|---|
| `sent` | payloads handed to the stack |
| `echoed` | payloads that came back with the same number and bytes |
| `ok` | delivery outcomes reported as ok (the acknowledgement arrived) |
| `failed` | delivery outcomes that are not ok: `retryExhausted` (no acknowledgement after the last resend) or `reset` |
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
came after the last resend on its side (`outcomesFailed`, with `retryExhausted`). A payload this side gave up on (`failed`) may never have
reached the board, and then leaves no board counter. The counters are read as described in What the board shows.

A run ends when every payload has its outcome and every echo is in. When a payload was given up on, its echo may never come, so the run
ends 3 seconds after the last outcome: the board's echoes still on their way arrive in that time. A payload given up on is no longer
counted as in flight, so the run goes on sending.

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

## The requests check

`vubcheck --link uart --port COMn --requests` and `vubcheck --link cdc --port COMn --requests` run the requests demo. The board must be
built with `VHAL_VUB_MODE=Requests` (the demo's README); on the echo image the requests get no reply, and the run fails. The demo is a
simulated actuator, and its command set is in `Common/Drivers/Interface/VUB/.tool/common/ActuatorCommands.h`, which the device and the
PC both compile:

| Command | Kind | What the device does | What the check compares |
|---|---|---|---|
| `ActuatorCommand::set` (0x12) | request, final | counts a set and answers at once with the token and the level | the final's token and level; a set's final is ok or busy |
| `ActuatorCommand::sweep` (0x13) | request, interims, final, cancel | gives eight steps at most, one per step time, then the final; a cancel stops it and the final has the cancelled status; a finished sweep sends the Delivered event `sweepDone` | the interims in order (step1 to stepN); one end per sweep, done or cancelled or operationCancelled; a Delivered event for each done sweep; the cancel handler runs at most once per cancel sent; one sweep is open at a time |
| `ActuatorEvent::note` (0x42) | plain event, PC to device | counted in the counters reply | the device's note count against the notes sent |
| `ActuatorEvent::tick` (0x41) | plain event, device to PC | a running number every 500 ms while the requester is connected | the numbers rise by one; a gap only under `--loss` |
| built-in ping (0xF0) | ping of the layer | answers each ping; the reply echoes the padding | each ping ok; the round trips are summarised as the lowest, average and highest |
| `ActuatorCommand::kick` (0x14) | no reply | counts a kick | the kick's delivery outcome (ok) and the device's kick counter |
| `ActuatorCommand::nudge` (0x15) | fire | counts a nudge; no packet is resent | the device's nudge counter against the nudges sent |
| `ActuatorCommand::hang` (0x16) | request, final | counts the request and never finishes it; the ticket times out after 300 ms | the final is a timeout or busy; the device's hang counter |
| `ActuatorCommand::absent` (0x7F) | request, final | not served by the device | the final is notSupported |
| `ActuatorCommand::counters` (0x17) | request, final | answers the device's counters | read before and after the run; the difference is compared |

Each run draws its requests from `--seed`: sets 30 %, sweeps 15 %, kicks 15 %, nudges 15 %, hangs 10 % and absent requests 15 %. A sweep
is sent only when no other sweep is open, so its interims are its own. A third of the sweeps are cancelled, one twelfth each at three
points: right after the send (before the REQUEST is acknowledged), after the first interim, and after the final, where the cancel must
find no request (`notFound`). A ping is sent with every fifth request, one at a time, with no padding and then 16 bytes of padding
alternately; a note is sent with every fourth request. The pings have a deadline of 3000 ms: under `--loss` a packet may need three resends
of 200 ms in each direction, which the layer's 1000 ms default does not cover. The device counters count from the board's last reset, so the run
reads them before and after, and compares the difference.

The summary has one line:

```
vubcheck link=<link> requests sent=<n> finals=<n> ok=<n> timeout=<n> busy=<n> notSupported=<n> cancelled=<n> invalid=<n> other=<n> duplicate=<n> unknown=<n> mismatched=<n> interims=<n> interimErrors=<n> sweeps=<done>/<device> delivered=<n> cancelSent=<n> cancelRuns=<device> cancelledEnds=<n> cancelMissed=<n> sets=<ok>/<device> kicks=<ok>/<device> nudges=<sent>/<device> hangs=<admitted>/<device> notes=<sent>/<device> ticks=<n> tickGaps=<n> pings=<ok>/<sent> pingFailed=<n> rttMinUs=<n> rttAvgUs=<n> rttMaxUs=<n> refused=<n> lateReplies=<n> eventsLost=<n> dropped=<n> inflight=<n> loss=<K> seed=<seed> seconds=<s> result=<PASS|FAIL>
```

`ok`, `timeout`, `busy`, `notSupported`, `cancelled` (the layer's operationCancelled) and `invalid` count the finals by category; `other`
is any other result, such as a delivery that gave up (`retryExhausted`). `sweeps` shows the done sweeps the PC got and the device's count;
`delivered` is the Delivered events the PC got; `cancelSent` is the cancels the layer accepted, `cancelRuns` the cancel handler runs the
device counted, and `cancelledEnds` the sweeps that ended cancelled. `sets`, `kicks` show the finals that were ok and the device's count;
`nudges` and `hangs` show what was sent or admitted and what the device ran; `notes` the notes sent and counted. `ticks` and `tickGaps`
count the device's ticks and the gaps in their numbers. `refused`, `lateReplies` and `eventsLost` are the PC layer's counters (Counted).
The `rtt` values are microseconds over the pings that came back.

A pass needs every final in its expected category, no duplicate, unknown or mismatched reply, every interim in order, one end per sweep,
and the device's counters as above. Without `--loss` the done sweeps, the Delivered events and the device's sweep count are equal; each
cancel handler runs at most once per accepted cancel; the notes are counted exactly; the ticks rise by one, and there are at least two ticks a
second of the run, less two; and every ping comes back ok with no late reply. With `--loss` the counts are bounded, not equal, and gaps in the ticks
are allowed. A cancel that the device ended first is not an error: its end is then done. Exit codes: 0 for `PASS`, 1 for `FAIL`, 2 for a usage error or a port that does not open. `--loss K` drops every K-th
outgoing frame on the PC side, with the guard of The loss pattern, as the echo check does. The requests check does not have the echo check's pacing limit of 4 payloads; it
has `--inflight` up to 8, the requests the device's layer serves at once.

The device's requests counters are in the status block after the echo words, and only in the requests image (`VHAL_VUB_MODE=Requests`). The
echo image's block is unchanged, 344 bytes; the requests image's block is 396 bytes, 99 words:

| Offset | Field |
|---|---|
| 344, 348, 352, 356, 360 | `requests.sets`, `sweeps`, `kicks`, `nudges`, `hangs`: the device's counters |
| 364 | `requests.interims`: interims given |
| 368 | `requests.interimBusy`: steps that waited for a busy interim slot |
| 372 | `requests.sweepsExpired`: sweeps the ticket ended before their final |
| 376 | `requests.sweepsActive`: sweeps in progress |
| 380 | `requests.unclaimed`: payloads the requests layer did not take |
| 384, 388, 392 | `requests.cancels`, `notes`, `eventBusy`: cancel handler runs, notes counted, events the device could not send yet |

## Results

The two images of the demo, from `arm-none-eabi-size` of the RelSpeed preset build. Flash is text plus data, RAM is data plus bss:

| Image | Flash | Share of 128 KB | RAM |
|---|---:|---:|---:|
| echo | 113656 bytes | 86.7 % | 14124 bytes |
| requests | 115044 bytes | 87.8 % | 12020 bytes |

The echo image does not contain the requests code, so its size is the one of the link check alone. The requests image
holds the device's cancel handlers, its Delivered and plain events and the answer to the pings, in each of its two stacks. The responder
is instantiated once per stack, because each stack has its own handler list (the sweep handlers carry the stack's index). Its growth over
the image without these features is 18304 bytes of flash and 232 bytes of RAM. The requests image has no hardware run in this section yet.

Measured on 2026-11-24 on an STM32G431 with the RelSpeed image of this demo (113584 bytes of text, 14124 bytes of data and bss), and
vubcheck built with MinGW g++ 15.2 on Windows 11, with the default `--inflight 4`. USART1 goes through a CH343 adapter (COM3); the CDC port
was COM13. Each line is one vubcheck run; each run is a new PC stack against the board's running stack, so every run after the first on a
link also checks a restart of the PC side. After the runs without `--loss 3` the board's status block showed every echo acknowledged
(`outcomesOk` equal to `received` on each stack) and every drop counter and UART error at zero.

| Run | Sent | Echoed | ok | Failed | Retried | Dropped | Rate | Result |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| `uart --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 0 | 260 per second | PASS |
| `cdc --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 0 | 262 per second | PASS |
| `bulk --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 0 | 266 per second | PASS |
| `hid --count 2000` | 2000 | 2000 | 2000 | 0 | 0 | 0 | 345 per second | PASS |
| `uart`, `cdc`, `bulk`, `hid` again | 200 | 200 | 200 | 0 | 0 | 0 | 253 to 347 per second | PASS |
| `uart --loss 5` | 200 | 200 | 200 | 0 | 51 | 100 | 19 per second | PASS |
| `cdc --loss 5` | 200 | 200 | 200 | 0 | 51 | 100 | 19 per second | PASS |
| `bulk --loss 5` | 200 | 200 | 200 | 0 | 40 | 100 | 16 per second | PASS |
| `hid --loss 5` | 200 | 200 | 200 | 0 | 60 | 145 | 16 per second | PASS |
| `bulk --loss 3 --count 500` | 500 | 483 | 483 | 17 | 179 | 483 | 11 per second | FAIL |
| `hid --loss 3 --count 500` | 500 | 493 | 493 | 7 | 107 | 599 | 10 per second | FAIL |

With `--loss 5` one frame in five of the PC side is dropped, acknowledgements included, and every payload still arrives once. The rate is
limited by the PC loop and the window of 4 payloads in flight.

### The loss pattern

`--loss K` drops every K-th frame of the PC side, and the drop is periodic. A resend comes at a spacing of whole frames, and that
spacing can be a multiple of K: then the four attempts of one packet all fall on dropped frames, and `Retries(3)` gives up on a packet
the board would have taken. Under `--loss 3`, the host loopback (`test/TestVubBoardLoopback.cpp`) showed this for packets 71 (seed 1 of
the echo mode), 135 and 188 (seed 2) and 78 (seed 3): each of their four attempts was a dropped frame, and each attempt fell on a frame
whose number is a multiple of 3.

The results in the table above were measured with the periodic drop alone, before the guard, and they are not measured again.

So the drop keeps a frame when its packet, or the packet it acknowledges, has already lost `Retries(3)` frames (three). A packet then
always has one attempt through, and a give-up under `--loss` is a fault of the stack, not of the pattern. The rule reads the two-byte
sequence layout of the board chains. The frame counts stay as they are: a kept frame is sent, and the summary's `dropped` counts only the
frames that were dropped.

Under `--loss 3` the echo check passes on each of six seeds of the loopback, and so does the requests check. The requests have a reply
deadline of 3000 ms, because a request can take its four attempts (600 ms) to arrive, and its final can then wait for a free place in the
device's window, which the lost acknowledgements of the PC hold. With a deadline of 1000 ms three of 24 runs of seed 5 ended one `set` with
`timeout` although the device had run it; with 5000 ms none of 24 did.

### Host loopback

`test/TestVubBoardLoopback.cpp` runs the PC side of this check (the chains and the runs of `src/`) against the demo's device stacks over
an in-memory UART line, with the device on a thread of its own. It is built with g++ and no board:

```sh
g++ -std=c++23 -Wall -Wextra -Werror -I Common -I .demo/STM32G4/Application -I Periphery -o loopback.exe Common/Drivers/Interface/VUB/.tool/board/test/TestVubBoardLoopback.cpp -lwinmm
loopback.exe echo|requests LOSS SEED... [-trace]
```

Each run prints the summary line of `vubcheck`. `-trace` adds one line per frame the PC side wrote, the frames that were dropped, the
packets without a device acknowledgement, and the per-kind tallies of the requests check. Results of this build, seeds 1 to 6, 200 payloads
or requests each:

| Mode | Loss | Result |
|---|---|---|
| echo | 0 | 6 of 6 PASS, `retried=0` |
| echo | 3 | 6 of 6 PASS, `retried` 57 to 79 |
| requests | 0 | 12 of 12 PASS (seeds 1 to 12) |
| requests | 3 | 12 of 12 PASS (seeds 1 to 12), `pingFailed=0` and no mismatch |

`--loss` drops only the PC's outgoing frames, so the device's ticks are not dropped and `tickGaps` stays at zero in these runs. The round trips
under loss reach about one second, which the 3000 ms ping deadline covers.

## Notes

- The session values differ on every boot when `sessionSource` is 1: the RNG gives four words, and each stack takes the low 16 bits
  of one of them. The fallback (2) can repeat across boots, so a check that reboots the board needs the RNG source.
- The status block is refreshed every 100 ms, so its counters can lag the last echo by that much.
- A board that is not flashed with this demo has no `1209:0007` device and no COM port for the bulk and HID checks.
