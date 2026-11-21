# usbbench

Host-side throughput and data-check benchmark for the USB benchmark firmware. It speaks the protocol in
`.tool/common/BenchProtocol.h` (vendor requests on the benchmark interface, bulk endpoints 0x81 IN and 0x02 OUT, the
xorshift32 pattern) and reports MB/s, data errors and the device counters after each run.

Two backends sit behind one interface:

- `libusb` (default): libusb-1.0 on Windows (through its WinUSB backend), Linux and macOS.
- `winusb` (Windows only): native WinUSB. SetupAPI finds the device by `BenchProtocol::InterfaceGuid`, the control pipe
  gets its timeout as the interface is opened, the bulk pipes get `RAW_IO`, `PIPE_TRANSFER_TIMEOUT` and
  `AUTO_CLEAR_STALL`, and several overlapped `WinUsb_ReadPipe`/`WritePipe` requests are in flight per direction. It is
  the baseline for the owner's driver comparison.

## Build

Windows (MinGW-w64 g++ 15, CMake, Ninja). The configure step downloads libusb 1.0.30 (the MinGW64 static library) from
the official GitHub release into `third_party/` and checks its SHA-256 before extracting it. `third_party/` is
git-ignored.

Run the commands from the `.tool` folder: `-S usbbench` and `-B build` are relative to it.

```
cmake -S usbbench -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

Linux and macOS use the system libusb-1.0 through pkg-config (`libusb-1.0` must be installed). The `winusb` backend is
not built there.

## Usage

```
usbbench list                    every attached benchmark device with its serial number and where it sits on the bus
usbbench info                    descriptors of the device (prints a clear message when no device is attached)
usbbench counters                the device counters
usbbench in <bytes>              device sends the pattern; host checks every byte
usbbench out <bytes>             host sends the pattern; the device checks it, see the device counters
usbbench loopback <bytes>        device returns the host data; host checks it
usbbench sourcesink <bytes>      both directions at once, each checked
usbbench reconnect <ms>          pull-up off for <ms>; prints the time until the device is back
```

Options:

- `--backend libusb|winusb`: default `libusb`.
- `--inflight N`: transfers in flight per direction, 1 to 32, default 8.
- `--transfer bytes`: bytes per transfer, a multiple of 64, up to 1 MiB, default 65536.
- `--serial text`: the device with this serial number, as `list` prints it. Every command except `list` accepts it.
  Each bench board reports its MCU unique ID as its serial number. The match ignores ASCII case, so `0a1b` and `0A1B`
  are the same board. A value that starts with `--` is refused, so `--serial --all` is a usage error and does not take
  `--all` as the serial.
- `--all`: runs `in`, `out`, `loopback` or `sourcesink` on every attached device at the same time, one thread and one
  backend per device. It prints one line per device (serial, MB/s per direction, mismatches, receive errors, result)
  and a total line. The total line gives the aggregate rate per direction: the bytes verified on all devices, divided
  by the time from the earliest transfer start to the latest transfer end. The total passes only when every device
  passed. `--all` is refused with `info`, `counters` and `reconnect`, and together with `--serial`.
- Sizes accept `k`, `m` and `g` suffixes (binary).

With one device attached, every command works without `--serial`, as before. With several attached and no `--serial`
or `--all`, a command prints the devices and exits `2`; it does not pick one.

Exit codes: `0` pass, `1` device or transport failure (no device, a failed or stalled transfer, a timeout), `2` usage
error, `3` data error (a mismatched byte, a device receive error, or a device byte count that does not match). With
`--all` the exit code is the worst device's: a transport failure on any device outranks a data error on another. If a
thread for a device cannot be created, no transfer runs and the run exits `1`.

Each run prints the throughput in MB/s (10^6 bytes) and MiB/s (2^20 bytes), the host check (bytes verified and
mismatches with the first mismatch offset) and the device counters.

## Design notes

- Byte counts and transfer sizes are multiples of 64 (the bulk packet size). Every transfer then ends on a packet
  boundary, which the device and both backends see the same way, and WinUSB `RAW_IO` requires it.
- Each run starts with `SetMode(Idle)`, a drain of bulk IN, then the run's mode. `SetMode` resets the device's
  generators and counters, and the drain discards data a previous stream left queued.
- Transfers complete in any order the backend reports, but the pattern is checked strictly in submission order, so
  the check always sees the device's byte stream in sequence (`BulkLane`).
- `reconnect` waits for the device to drop off the bus before it waits for it to come back, so it does not measure the
  old enumeration. The time covers the pull-up off time, the disconnect and the re-enumeration. A board with a fixed
  pull-up never disconnects; the command reports that instead of passing.
- `info` only reads descriptors. Nothing else is run unless a command is given.
- The device counters are decoded field by field from their little-endian layout, not by copying the struct.
- Boards are told apart by serial number, which is the string descriptor the firmware fills from the MCU unique ID.
  libusb reads it with `libusb_get_string_descriptor_ascii`; WinUSB with `WinUsb_GetDescriptor` on the English language
  ID (0x0409), so both backends print and match the same text. The device path has no serial in it, so WinUSB opens a
  device to read its string. `Open(serial)` keeps only the device that matches open, and closes the others again. The
  comparison ignores ASCII case (`SameSerial`), and so does the duplicate check of `--all`.
- `Open` reports the error that explains why it opened no board (`OpenFailureText`). A board that was the one asked for
  and failed to open or claim shows its own error. A serial is reported as not attached only when every other board
  could be read. A board whose serial could not be read might have been the one asked for, so the message then says
  that the serial cannot be checked and gives the read error. Without `--serial`, the error of the device that failed is
  shown.
- `list` shows where each board sits so two boards with the same serial number can be told apart. libusb prints the bus
  and port path. WinUSB prints the device instance segment of the interface path (the text between the second and third
  `#`). Windows can build that instance ID from the serial number, so on some boards the column repeats the serial.
- One device attached and no `--serial`: the first matching device, which is the behaviour before `--serial` existed.
  Several attached and no `--serial`: the command refuses and lists them. A run on the wrong board still ends in PASS,
  and "first in the USB enumeration order" changes with the port, so the tool does not guess.
- `--all` opens every device before the first run starts. A board that is missing, busy or has no readable or unique
  serial stops the whole run, so a parallel result never covers only some of the boards. Each device then runs on its
  own backend in its own thread, and the threads share only the read-only options. Each thread does its setup (the
  control requests and the drain) and then waits at a start gate (`StartGate`). When the last thread is ready they all
  start their transfers, so the devices share one time window. A device whose setup failed still arrives at the gate,
  so it does not hold the others back.
- The total line of `--all` is aggregate, not a sum of the per-device rates. For each direction it is the bytes verified
  on all devices divided by the time from the earliest transfer start to the latest transfer end. Each device's own
  start and end bound its transfers, and a device that never ran its transfers adds nothing. Boards behind one shared hub
  divide that bandwidth, and the total shows it.
- If a thread cannot be created, the threads already started are released from the gate with no transfer, joined, and
  the run exits `1`. If an exception escapes a device's thread before the gate, the gate is cancelled the same way, so
  the other devices never wait for a thread that will not arrive.
- Two places stop the process with `std::abort()` instead of returning an error: a libusb transfer that still has not
  reported back after it was cancelled, and a WinUSB request (or drain read) that still has not ended after its abort.
  Both wait several bulk timeouts first. The transfer buffers and the OVERLAPPED structures still belong to the USB stack
  until the request ends, so returning would free memory the OS may still write into. With `--all`, that abort ends the
  run for every device, not only the hung one. This is accepted rather than redesigning the backends. It happens only
  when a board stops answering and the cancel does not complete.
- `reconnect` is refused with `--all`. It pulls one board off the bus and times that board's return. With several boards
  disconnecting at once the timings and the "did not disconnect" check would mix up which board did what.
- `DeviceSelection` decides the device, the `--all` preconditions and the open failure message without any hardware,
  `Summary` holds the verdict, the exit code mapping and the totals, and `StartGate` holds the start line. All are unit
  tested. The per-device run itself needs a board.

## Layout

```
CMakeLists.txt          build (Ninja or any CMake generator)
cmake/Libusb.cmake      libusb 1.0.30 download and pin on Windows, pkg-config elsewhere
src/BulkLane.*          pattern check and in-order verification of one bulk direction
src/UsbBackend.h        backend interface, timeouts, the no-device message
src/LibUsbBackend.*     libusb backend
src/WinUsbBackend.*     native WinUSB backend (Windows only)
src/BackendFactory.cpp  backend selection
src/Options.*           command line parsing and usage
src/DeviceSelection.*   which device a command acts on, with and without --serial and --all; serial matching
src/Summary.*           run verdicts, exit codes and the --all totals
src/Text.h              printf-style text appended to a report string
src/ExitCodes.h         the exit code values
src/StartGate.h         the start line of an --all run
src/Commands.*          the commands, the report and the --all run
src/main.cpp            entry point
test/*Test.cpp          offline tests (lane scheduling, options, device selection, summary, start gate), no device needed
```
