# usbbench

Host-side throughput and data-check benchmark for the USB benchmark firmware. It speaks the protocol in
`../common/BenchProtocol.h` (vendor requests on the benchmark interface, bulk endpoints 0x81 IN and 0x02 OUT, the
xorshift32 pattern) and reports MB/s, data errors and the device counters after each run.

Two backends sit behind one interface:

- `libusb` (default): libusb-1.0 on Windows (through its WinUSB backend), Linux and macOS.
- `winusb` (Windows only): native WinUSB. SetupAPI finds the device by `BenchProtocol::InterfaceGuid`, the pipes get
  `RAW_IO`, `PIPE_TRANSFER_TIMEOUT` and `AUTO_CLEAR_STALL`, and several overlapped `WinUsb_ReadPipe`/`WritePipe`
  requests are in flight per direction. It is the baseline for the owner's driver comparison.

## Build

Windows (MinGW-w64 g++ 15, CMake, Ninja). The configure step downloads libusb 1.0.30 (the MinGW64 static library) from
the official GitHub release into `third_party/` and checks its SHA-256 before extracting it. `third_party/` is
git-ignored.

```
cmake -S usbbench -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build
```

Linux and macOS use the system libusb-1.0 through pkg-config (`libusb-1.0` must be installed). The `winusb` backend is
not built there.

## Usage

```
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
- Sizes accept `k`, `m` and `g` suffixes (binary).

Exit codes: `0` pass, `1` device or transport failure (no device, a failed or stalled transfer, a timeout), `2` usage
error, `3` data error (a mismatched byte, a device receive error, or a device byte count that does not match).

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
src/Commands.*          the commands and the report
src/main.cpp            entry point
```
