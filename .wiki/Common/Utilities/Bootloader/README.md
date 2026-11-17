# Bootloader

Extensible firmware bootloader framework with command protocol, Flash memory support, optional AES encryption, and pluggable communication transports.

## Table of Contents

- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [ICommunication -- Communication Interface](#icommunication----communication-interface)
- [IBootloader -- Core Protocol Engine](#ibootloader----core-protocol-engine)
  - [Commands](#commands)
  - [Packet Format](#packet-format)
  - [Firmware Header](#firmware-header)
  - [States](#states)
  - [Write Stream](#write-stream)
  - [Timing and Transport](#timing-and-transport)
  - [Virtual Methods to Implement](#virtual-methods-to-implement)
- [FLASHBootloader -- Flash Memory Bootloader](#flashbootloader----flash-memory-bootloader)
- [AESFLASHBootloader -- Encrypted Bootloader](#aesflashbootloader----encrypted-bootloader)
- [NRF8001Communication -- BLE Transport](#nrf8001communication----ble-transport)
- [Firmware Update Workflow](#firmware-update-workflow)
  - [Retrying after a lost response](#retrying-after-a-lost-response)
- [API Reference](#api-reference)

## Architecture

The library follows a layered design:

```
+--------------------------+
|   Your Concrete Class    |  <-- Implements platform-specific logic
+--------------------------+
| AESFLASHBootloader (opt) |  <-- Adds AES encryption layer
+--------------------------+
|    FLASHBootloader        |  <-- Flash read/write/erase via AFLASH adapter
+--------------------------+
|      IBootloader          |  <-- Protocol engine, command parsing, state machine
+--------------------------+
|    ICommunication         |  <-- Pluggable transport (UART, BLE, USB, etc.)
+--------------------------+
```

`IBootloader` manages the full protocol -- command parsing, state transitions, data buffering, async erase, SHA256 hashing, and encryption hooks. Inheritors only implement platform-specific operations.

## Quick Start

```cpp
#include <Utilities/Bootloader/Loader/FLASHBootloader.h>

// 1. Implement a communication transport
class UARTCommunication : public ICommunication {
    AUART& uart;
    DataReceivedCallback callback;
public:
    UARTCommunication(AUART& u) : uart(u) {}

    ResultStatus Initialize() override { return uart.Initialize(); }
    ResultStatus SendData(std::span<const uint8> data) override {
        return uart.Send(data.data(), data.size());
    }
    bool IsReadyToSend() const override { return true; }
    bool IsConnected() const override { return true; }
    void SetDataReceivedCallback(DataReceivedCallback cb) override { callback = cb; }
    size_t GetMaxPacketSize() const override { return 240; }
};

// 2. Implement a concrete bootloader
class MyBootloader : public FLASHBootloader<> {
public:
    MyBootloader(ICommunication& comm, AFLASH& flash)
        : FLASHBootloader(comm, flash, 0x08020000, 0x60000) {}

protected:
    bool OnCheckBootloaderRequest() const override {
        // Enter bootloader if button is pressed
        return GPIO_ReadPin(BOOT_BUTTON) == 0;
    }

    bool OnStartApplication() override {
        uint32 appAddr = GetMemoryStartAddress();
        uint32 sp = *reinterpret_cast<uint32*>(appAddr);
        uint32 entry = *reinterpret_cast<uint32*>(appAddr + 4);
        SCB->VTOR = appAddr;
        __set_MSP(sp);
        reinterpret_cast<void(*)()>(entry)();
        return true;
    }

    ResultStatus OnEraseIteration(
        uint32 address, uint32 endAddress,
        uint32 iteration, uint32& bytesErased
    ) override {
        uint32 sector = GetSectorFromAddress(address);
        uint32 sectorSize = GetSectorSize(sector);
        flashAdapter.SectorErase(sector);
        bytesErased = sectorSize;
        return (address + sectorSize >= endAddress) ? ResultStatus::ok : ResultStatus::inProgress;
    }
};

// 3. Wire everything together
UARTCommunication uart(uartAdapter);
MyBootloader bootloader(uart, flashAdapter);

int main() {
    bootloader.Initialize();
    while (true) {
        bootloader.Process();
    }
}
```

## ICommunication -- Communication Interface

Abstract interface for plugging in any transport layer (UART, BLE, USB, SPI).

```cpp
class ICommunication {
public:
    using DataReceivedCallback = std::function<void(std::span<const uint8>)>;

    virtual ResultStatus Initialize() = 0;
    virtual ResultStatus SendData(std::span<const uint8> data) = 0;
    virtual bool IsReadyToSend() const = 0;
    virtual bool IsConnected() const = 0;
    virtual void SetDataReceivedCallback(DataReceivedCallback callback) = 0;
    virtual size_t GetMaxPacketSize() const = 0;
    virtual void Process() {}  // Default no-op; override for polling transports
};
```

Key contract:
- `Process()` must be non-blocking (< 10 ms).
- `DataReceivedCallback` should only be called with valid, CRC-checked data, from the same context as `Process()`.
- An empty span passed to the callback means "the transport lost framing (reconnect, or a message it could not complete)". The bootloader then drops its partial input and the pending response. It does not abort an erase or change the write stream, so a transport may call it on every disconnect and whenever it gives up on a message (see [NRF8001Communication](#nrf8001communication----ble-transport)).
- `SendData()` returns `ResultStatus::ok` when the data was sent, `ResultStatus::busy` when the transport cannot take it yet (the bootloader keeps the response and retries once `IsReadyToSend()` is true), `ResultStatus::notReady` when not connected, and any other status when the data was dropped.
- `GetMaxPacketSize()` returns payload size without protocol overhead.

## IBootloader -- Core Protocol Engine

Template class that implements the full bootloader protocol.

```cpp
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class IBootloader;
```

| Template Parameter | Default | Description |
|-|-|-|
| `RxBufferSize` | 1024 | Receive ring buffer size |
| `AccumBufferSize` | 512 | Write data accumulation buffer |
| `PacketDataMaxSize` | 240 | Maximum data payload per packet |

### Commands

| Command | Code | Description |
|-|-|-|
| `Ping` | `0x01` | Connection check |
| `GetInfo` | `0x02` | Returns device info (version, memory layout, protection) |
| `Erase` | `0x10` | Erases entire firmware area (async). Restarts the write stream (see [Write Stream](#write-stream)) |
| `Write` | `0x11` | Streams firmware data (auto-increments write position). Retries with the same sequence are answered without writing twice |
| `Read` | `0x12` | Reads firmware data back. Payload length 0: streaming form (auto-increments read position, only once the response has been sent). Payload length 4: optional little-endian `uint32` offset, retry-safe, does not move the read position (see [Read at an offset](#read-at-an-offset)). Any other length answers `InvalidParameters`. Refused with `Protected` in AES mode and under read-out protection |
| `Verify` | `0x13` | Validates firmware CRC32 against the stored header |
| `Finalize` | `0x14` | Checks the image against the expected `FirmwareHeader` sent by the host, then writes the header. Payload: 8 bytes (crc32, size, little-endian). Repeating it after a lost response answers `Success` without rewriting the header when the same header is already stored |
| `ResetWritePos` | `0x15` | Restarts the write stream at position 0 |
| `ResetReadPos` | `0x16` | Resets read position to 0 |
| `GetHash` | `0x17` | Returns SHA256 hash of all raw data sent so far (ciphertext in AES mode) |
| `Start` | `0x20` | Answers `Success`, then launches the application about 150 ms later so the response is sent first. Answers `Error` when `checkFirmware` finds no valid image, and `Busy` while an erase is running |
| `Reset` | `0x21` | Answers `Success`, then resets the device about 150 ms later |
| `GetStatus` | `0x30` | Returns current state and bytes received (see [API Reference](#api-reference)) |
| `UnlockDevice` | `0x40` | Removes read protection (requires password; implemented by the inheritor, see `UnlockWithPassword`) |
| `UserData` | `0x50` | Custom read/write data handler |

### Packet Format

**Command packet** (host to device):

| Field | Size | Value |
|-|-|-|
| `header` | 1 | `0xAA` |
| `sequence` | 1 | Packet sequence number |
| `command` | 1 | Command code |
| `length` | 1 | Data length |
| `data` | 0..240 | Command-specific payload |

A packet with `length` greater than `PacketDataMaxSize` is invalid. The bootloader drops only the `0xAA` byte, answers `InvalidParameters` with the sequence from the packet, and resynchronises on the next `0xAA`. It never copies more than `4 + PacketDataMaxSize` bytes.

**Response packet** (device to host):

| Field | Size | Value |
|-|-|-|
| `header` | 1 | `0xBB` |
| `sequence` | 1 | Echo of request sequence |
| `status` | 1 | `BootloaderStatus` code |
| `length` | 1 | Data length |
| `data` | 0..240 | Response payload |

### Response Statuses

| Status | Code | Meaning |
|-|-|-|
| `Success` | `0x00` | OK |
| `Error` | `0x01` | General error (for example: erase failed, flash write failed, unaligned write, image not aligned to a cipher block at Finalize) |
| `InvalidCommand` | `0x02` | Unknown command |
| `InvalidParameters` | `0x03` | Bad parameters (bad length, accumulator overflow, wrong Finalize payload) |
| `Busy` | `0x04` | Device busy (an erase is running) |
| `NotReady` | `0x05` | Not ready |
| `Protected` | `0x06` | Memory protected: `Read` under read-out protection (RDP Level 1) or in AES mode; `Erase`, `Write` and `Finalize` under read-out protection unless `allowUpdatesWhenReadProtected` is set |
| `CryptoError` | `0x07` | Decryption failure (the decryption hook returned no data) |
| `AccessDenied` | `0x08` | Refused by the access policy (`OnCheckAccessLevel()` returns `accessError`) |
| `EndOfData` | `0x09` | No more data to read |
| `ChecksumMismatch` | `0x0A` | Image in flash does not match the checksum sent by the host at Finalize. The header is not written |

### Firmware Header

Stored at the **end** of the firmware memory region (`startAddress + memorySize - 8`):

```cpp
struct FirmwareHeader {
    uint32 crc32;   // CRC32 of the first `size` bytes of firmware (plaintext in AES mode)
    uint32 size;    // Firmware size in bytes
};
```

The host sends the same 8-byte layout (little-endian `crc32`, then `size`) as the `Finalize` payload. The device checks it against the image before writing the header (see [Firmware Update Workflow](#firmware-update-workflow)). `Start` and `Verify` use the stored header to decide whether the image is valid.

### States

`State` values are sent on the wire by `GetStatus`, so they are fixed:

| State | Value | Description |
|-|-|-|
| `Idle` | `0` | Waiting for commands |
| `Receiving` | `1` | Kept for wire compatibility; not entered by the current firmware |
| `Processing` | `2` | Executing an erase (sends `Busy` to new requests) |
| `Error` | `3` | Kept for wire compatibility; not entered by the current firmware. A failed Write is reported through the write stream (see [Write Stream](#write-stream)) |

### Write Stream

The firmware image is streamed with `Write` commands. The bootloader keeps the stream state (write position, bytes received, the hasher, and the decryption state in AES mode). The stream is restarted by `Erase` and `ResetWritePos`, which both reset it to position 0 and clear the failure state. `Erase` also resets the read position.

- **Duplicate Write.** A `Write` whose sequence equals the sequence of the last accepted `Write` is answered with the status that Write produced. No data is touched. A host can therefore retry a `Write` after a lost response without writing the data twice.
- **Write failure.** When a Write fails (flash write error, decryption failure, or misaligned data), the bootloader answers the error and enters the failed stream state. Every later `Write` then answers the same error, including retries with the same sequence. Only `Erase` or `ResetWritePos` clears this state.
- **Finalize and the failure state.** `Finalize` answers the stored write error if the stream failed. It also answers `Error` when an incomplete cipher block is still waiting (for example an AES image whose length is not a multiple of 16), because the image is truncated.
- **Write alignment.** Only the final chunk of an image may be shorter than the write alignment (`GetWriteAlignment()`). Any write after a short chunk is refused with `Error`. Writes must end at or before the firmware header at the end of the region (`GetFirmwareHeaderAddress()`).

### Read at an Offset

`Read` has two forms, chosen by the payload length:

- **Length 0 (streaming).** Returns the next bytes from the read position and advances it once the response has been sent. This is the original behaviour. A lost response loses that data, so it is not retry-safe; use the offset form after a timeout.
- **Length 4 (offset).** The payload is a little-endian `uint32` offset into the stored image. The response carries `min(PacketDataMaxSize, bytesWritten - offset)` bytes from that offset, passed through `OnEncryptData()` like the streaming form. The read position is not touched, so the same request can be repeated and returns the same bytes. An offset at or beyond the bytes written answers `EndOfData`. The same refusals apply as for the streaming form (`Protected` in AES mode and under read-out protection).

A host can therefore download the image in `240`-byte steps with offsets `0`, `240`, `480`, and so on, and repeat any step after a lost response.

### Timing and Transport

- **Partial packets.** `packetTimeoutMs` (public, default `10000`, `0` disables) bounds how long an incomplete packet may wait at the front of the receive buffer. The timer starts when the incomplete packet reaches the front, not from the last byte received, so steady traffic after a stale header does not keep it alive. When the timeout expires, the bootloader drops the `0xAA` byte and resynchronises. Set it longer than the transport needs for one full packet including its retries (CompactStreamingProtocol retries a fragment after 5 s).
- **Busy transport.** If the transport reports `busy`, the bootloader keeps one pending response and sends it from a later `Process()` once `IsReadyToSend()` is true. While a response is pending, the bootloader does not parse new packets, start an erase step, or perform a deferred Start or Reset. A second response while one is pending is dropped. A queued streaming `Read` (length 0) advances the read position only when its data is actually sent, so no data is skipped.
- **Start and Reset delay.** `Start` and `Reset` answer `Success` first. The bootloader then waits `150 ms` (an internal constant, `responseFlushDelayMs`) and performs the jump or reset from `Process()`, after the response has been flushed.
- **Empty-span callback.** An empty span from the transport means the transport lost framing (for example a reconnect, or an abandoned message). It clears partial input and drops the pending response, including a queued erase completion. It does not change the erase or the write stream, so an erase started before a reconnect still completes on the device. After a reconnect the host should re-query with `GetStatus` to learn the state of an erase.

### Virtual Methods to Implement

**Required (pure virtual):**

| Method | Purpose |
|-|-|
| `OnCheckBootloaderRequest()` | Return `true` to enter bootloader, `false` to start app |
| `OnStartApplication()` | Jump to application (deinit peripherals, set VTOR, set MSP) |
| `GetMemoryStartAddress()` | Application memory start address |
| `GetMemorySize()` | Application memory size |
| `OnWriteMemory(addr, data)` | Write data to memory. Must accept the aligned prefix and the final short chunk; see `GetWriteAlignment()` |
| `OnReadMemory(addr, data)` | Read data from memory |
| `OnEraseIteration(addr, end, iter, &erased)` | Erase one sector/page per call. Return `ResultStatus::ok` when finished, `ResultStatus::inProgress` to continue. Any other status stops the erase with `Error` |

**Optional (with defaults):**

| Method | Default | Purpose |
|-|-|-|
| `OnBeforeInitialize()` | No-op | Early init (e.g., Flash setup). Return an error to make `Initialize()` fail (for example a wrong AES key size) |
| `OnCheckAccessLevel()` | Allow | Whether flash changes are allowed at all. When it returns an error, `Initialize()` returns that error before the transport starts (FLASHBootloader: RDP Level 2) |
| `OnInitialize()` | No-op | Post-check initialization |
| `OnProcess()` | Runs async ops | Main loop hook |
| `GetProtectionLevel()` | `nullopt` | Return RDP level for `GetInfo` |
| `IsMemoryProtected()` | `false` | Read-out protection. Refuses `Read` and, unless `allowUpdatesWhenReadProtected` is set, `Erase`, `Write` and `Finalize`; `UnlockDevice` is needed only while it returns `true` |
| `IsReadBackAllowed()` | `true` | Return `false` to refuse `Read` with `Protected` (used by AES mode) |
| `GetWriteAlignment()` | `1` | Required alignment of write addresses and of all chunks except the last one (see [Write Stream](#write-stream)) |
| `OnStreamReset()` | No-op | Called when the write stream restarts (`Erase`, `ResetWritePos`). Restore per-image state such as a decryption chain |
| `OnEncryptData(data)` | Passthrough | Encrypt data for Read responses |
| `OnDecryptData(data)` | Passthrough | Decrypt data from Write commands. Return an empty span on failure |
| `OnHasEnoughData(buffer)` | `size > 0` | Check if buffer has enough for processing |
| `OnGetRequiredBytes(buffer)` | All | How many bytes to consume |
| `OnFinalizeFirmware()` | Write header | Create the header from `bytesWritten` (called after the host checksum check) |
| `OnUnlockDevice(password)` | Not supported | Remove read protection. Implement it with `UnlockWithPassword` (FLASHBootloader); there is no default password |

### Public API

```cpp
// Initialize bootloader (checks boot request, starts communication)
ResultStatus Initialize();

// Main loop -- call repeatedly
void Process();

// Attempt to start the application
bool StartApplication();

// Register a handler for UserData commands
void SetUserDataHandler(UserDataHandler handler);

// Set bootloader version reported by GetInfo
void SetBootloaderVersion(uint64 version);
```

#### Public Knobs

| Member | Default | Description |
|-|-|-|
| `checkFirmware` | `true` | `Start` checks the stored firmware header first and answers `Error` when it is invalid |
| `packetTimeoutMs` | `10000` | Partial packet timeout in ms, measured per packet from when it reaches the front of the receive buffer; `0` disables it (see [Timing and Transport](#timing-and-transport)) |
| `requireFinalizeChecksum` | `true` | When `false`, `Finalize` with an empty payload is accepted and writes the header without a checksum check. Use only for legacy hosts that do not send the expected header |
| `allowUpdatesWhenReadProtected` | `false` | Allows `Erase`, `Write` and `Finalize` while read-out protection is active. Images are not authenticated (the AES modes encrypt but do not authenticate), so with this flag anyone on the transport can install code that reads out the rest of the flash, including the bootloader and its keys. Enable it only when the images are authenticated, for example by a signature checked in `OnFinalizeFirmware()` |

#### UserData Handler

```cpp
bootloader.SetUserDataHandler(
    [](uint8 operation, uint8 dataType, std::span<const uint8> data)
        -> std::span<const uint8>
    {
        if (operation == 0) {  // Read
            if (dataType == 0x01) return batteryLevel;
        } else {               // Write
            if (dataType == 0x02) saveConfig(data);
        }
        return {};
    }
);
```

## FLASHBootloader -- Flash Memory Bootloader

Extends `IBootloader` with `AFLASH` adapter integration. Provides concrete implementations of `OnWriteMemory`, `OnReadMemory`, `OnCheckAccessLevel`, `GetWriteAlignment`, and memory parameter methods. `OnUnlockDevice` has no default; an inheritor implements it, usually with `UnlockWithPassword`.

```cpp
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class FLASHBootloader : public IBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize>;
```

Constructor:

```cpp
FLASHBootloader(
    ICommunication& comm,
    AFLASH& flash,
    uint32 startAddr,
    uint32 size,
    uint32 writeAlignment = 4
);
```

| Parameter | Description |
|-|-|
| `startAddr`, `size` | Firmware region. The last 8 bytes hold the `FirmwareHeader` |
| `writeAlignment` | Write granularity in bytes. Default `4`. `0` is treated as `1`. Must be a power of two up to `maxWriteAlignment` (8, the header size), and `startAddr` and `size` must be multiples of it; otherwise `Initialize()` fails with `invalidParameter`. Set it to the flash programming unit of the target (4 on STM32F4, 8 on STM32G0/G4) |

Write alignment:
- `OnWriteMemory` requires an aligned address. The aligned part of a chunk is written directly.
- The tail of a chunk that is shorter than `writeAlignment` is padded with `0xFF` and written as one unit. The adapter is never asked to read past the source data.
- Only the final chunk of an image may be unaligned. The host can send 240-byte packets and a short tail, for example `1001` bytes as four 240-byte packets and one 41-byte packet, with `writeAlignment = 4`.

**Still requires implementation of:**
- `OnCheckBootloaderRequest()`
- `OnStartApplication()`
- `OnEraseIteration(...)`

Protection levels from the Flash adapter are mapped automatically:
- `Level0` -- no protection, full access
- `Level1` -- read-out protection: `Read` answers `Protected`; `Erase`, `Write` and `Finalize` answer `Protected` unless `allowUpdatesWhenReadProtected` is set. Unlocking (`OnUnlockDevice`) removes the protection
- `Level2` -- flash changes are refused by policy: `Initialize()` returns `accessError` and the transport is not started

#### Unlocking Read-Out Protection

The `UnlockWithPassword` helper checks a password in constant time and then calls `flashAdapter.DisableReadProtection()`. There is no default password. The expected value must come from the application's own secure storage.

The protocol requires at least 16 password bytes (a shorter `UnlockDevice` answers `InvalidParameters`), and the helper requires the password to have exactly the size of the expected value. The example therefore uses a 16-byte `std::array`:

```cpp
class MyBootloader : public FLASHBootloader<> {
protected:
    ResultStatus OnUnlockDevice(std::span<const uint8> password) override {
        // 16 bytes is the minimum the protocol accepts; the value comes from secure storage
        static const std::array<uint8, 16> expectedPassword = { /* from secure storage */ };
        return UnlockWithPassword(password, expectedPassword);
    }
};
```

On STM32, removing read-out protection (RDP level 1 to 0) mass-erases the flash. The host must expect this after a successful unlock.

## AESFLASHBootloader -- Encrypted Bootloader

Adds transparent AES encryption/decryption to `FLASHBootloader`. Supports AES-128, AES-192, and AES-256 with ECB, CBC, and CTR modes.

```cpp
template<AESKeySize KeySize, size_t RxBufferSize = 1024,
         size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class AESFLASHBootloader : public FLASHBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize>;
```

### Usage

```cpp
uint8 key[32] = { /* AES-256 key */ };
AES<AESKeySize::AES256>::IV iv = { /* initialization vector */ };

class MyEncryptedBootloader : public AESFLASHBootloader<AESKeySize::AES256> {
public:
    MyEncryptedBootloader(ICommunication& comm, AFLASH& flash)
        : AESFLASHBootloader(comm, flash, AESMode::CBC, key,
                             0x08020000, 0x60000, iv) {}

    // ... implement OnCheckBootloaderRequest, OnStartApplication, OnEraseIteration
};
```

The key is taken as `std::span<const uint8>`, so a raw array, a `std::array` or a span all work. When the argument has a static extent (a raw array or a `std::array`), a size mismatch with `KeySize` is a compile error. A dynamic span with the wrong size makes `Initialize()` fail with `invalidParameter`.

### Modes

| Mode | Chaining | Image length | Notes |
|-|-|-|-|
| `ECB` | None | Multiple of 16 bytes | Blocks are independent |
| `CBC` | Via `currentIV`; restarted by `Erase` and `ResetWritePos` | Multiple of 16 bytes | A partial final block makes `Finalize` answer `Error` |
| `CTR` | One continuous stream over the whole image | Any size | Standard AES-CTR, see below |

**CTR stream.** The keystream for byte `o` of the image (counted from 0 since the last `Erase` or `ResetWritePos`) is byte `o % 16` of `AES-ECB(IV + o / 16)`. The counter is a 128-bit big-endian integer with carry, the same convention as standard AES-CTR. A host that encrypts the whole image with one continuous AES-CTR stream and this IV produces data that the device decrypts correctly, in any packet sizes. The device does not call `SetIV` per chunk.

**CBC.** The chain is restarted by `Erase` and `ResetWritePos`, so a second upload in the same session (after `Erase`) decrypts correctly.

**Read-back.** Read-back is disabled in AES mode: `Read` answers `Protected`.

### Convenience Aliases

```cpp
using AES128FLASHBootloader = AESFLASHBootloader<AESKeySize::AES128>;
using AES192FLASHBootloader = AESFLASHBootloader<AESKeySize::AES192>;
using AES256FLASHBootloader = AESFLASHBootloader<AESKeySize::AES256>;

// With custom buffer sizes
template<size_t RxSize, size_t AccumSize>
using AES128FLASHBootloaderCustom = AESFLASHBootloader<AESKeySize::AES128, RxSize, AccumSize>;
```

### IV Management

```cpp
bootloader.SetIV(newIV);          // Set new IV; resets the CBC chain and the CTR stream offset
auto currentIV = bootloader.GetIV();
auto mode = bootloader.GetMode(); // AESMode::ECB / CBC / CTR
constexpr auto ks = MyEncryptedBootloader::GetKeySize();
```

### Security Notes

- The destructor wipes `iv`, `currentIV` and the decryption buffer. AES does not wipe its round keys.
- Decryption errors return an empty span (never partial data). The write stream then fails with `CryptoError` (see [Write Stream](#write-stream)).
- ECB/CBC modes require data aligned to 16-byte blocks; CTR works with any size.
- For CBC, the IV chains across consecutive decrypt calls and restarts on `Erase` and `ResetWritePos`.

## NRF8001Communication -- BLE Transport

`ICommunication` implementation for the Nordic NRF8001 BLE chip, using `CompactStreamingProtocol` for packet fragmentation over 20-byte BLE characteristics.

```cpp
template<size_t MaxPacketCount = 4, size_t BufferSize = 512>
class NRF8001Communication : public ICommunication;
```

### Usage

```cpp
NRF8001Communication<> bleCom(spiAdapter, reqnPin, rdynPin, resetPin, "MyDevice");

bleCom.Initialize();
// In main loop:
bleCom.Process();  // Polls BLE stack, handles connections, processes data
```

### BLE Service Layout

| UUID | Role |
|-|-|
| `00004242-...` | Bootloader service |
| `00004243-...` | RX characteristic (BLEWrite) |
| `00004244-...` | TX characteristic (BLENotify) |

The transport handles BLE connection/disconnection events, protocol state reset on reconnection, and asynchronous multi-packet transmission via `CompactStreamingProtocol`.

Receive path:
- Each BLE write is copied in the BLE event handler into a staging ring buffer. `Process()` then drains the staging buffer into the protocol. The protocol is never called from inside the BLE driver's event processing, so an ACK is never sent re-entrantly.
- Several writes in one `Process()` pass are all kept.
- Empty data is ignored.
- On connect and disconnect the protocol is reset, the staging buffer is cleared, and the bootloader receives an empty span (drop partial input, see [Timing and Transport](#timing-and-transport)).

Transmit path:
- `IsReadyToSend()` is `true` only while connected and no fragment transfer is in progress. The bootloader holds its response until then.
- `SendData()` returns `notReady` when not connected, `busy` while the protocol is transmitting, and `error` when the protocol fails. It returns `ok` once the protocol has accepted the message, even if the first fragment could not be handed to the BLE stack yet: the protocol retries that fragment from its timer.
- A TX timeout does not reset the bootloader by itself. Lost fragments are retransmitted by the protocol. Only a message that is abandoned after its retries triggers the empty-span callback (see below).

Receive path, CRC failures: a fragment whose CRC does not match drops one byte and the parser scans on, so a valid fragment directly behind a corrupted one is delivered in the same pass without waiting for a retransmission.

Abandoned messages (both directions):
- Outgoing: a fragment that is not acknowledged within its retries (about 20 s: four timeouts of 5 s) abandons the message. The protocol reports `Timeout` and the transport calls the bootloader with an empty span, which drops partial input and the pending response. The erase and the write stream are not changed. The host sees no response to the command and retries it.
- Incoming: a message that stops before its last fragment is closed when the next message starts. The transport calls the bootloader with an empty span at that point, so the partial input is dropped before the new message is parsed.

### Fragment Format (v2)

Every fragment has the same layout: `typeAndFlags` (1 byte), `payloadSize` (1 byte), `fragmentIndex` (1 byte), payload (up to 16 bytes with 20-byte BLE packets), CRC8. An ACK has no payload.

`typeAndFlags` bit table:

| Bits | Field | Value |
|-|-|-|
| 0 | Packet type | `0` = Data, `1` = Ack |
| 1 | Last fragment | `1` on the final data fragment of a message (also on the only fragment of a one-fragment message). Always `0` in ACKs |
| 2..7 | Message id | `0..63`. Data fragments carry the id of their message. An ACK carries the id of the fragment it acknowledges |

`fragmentIndex` counts the fragments of one message from `0`. An ACK echoes the index of the fragment it acknowledges.

This format is not compatible with the previous one, in which bit 1 was a message toggle and the message id did not exist. Hosts must implement this format on both sides.

Sender rules:
- Each new message takes the next message id, incremented modulo 64. A reset does not restart the count.
- Every data fragment of the message carries its id. The final fragment also carries the last flag. A retransmission is byte-identical to the original fragment.
- An ACK is accepted only while a message is in progress and its message id and fragment index equal the message and the fragment just sent. Any other ACK (a stale ACK for an earlier message or fragment) is ignored.

Receiver rules, for each data fragment with a valid CRC, in this order:
1. Retransmission: the id and index equal those of the last accepted fragment. The fragment is ACKed again and nothing else happens. The payload is not delivered twice.
2. Index 0: a new message. If the previous message never got its last fragment, the receive reset happens first (the bootloader gets an empty span). Then the fragment is ACKed, its payload is delivered if it is not empty, and it opens the new message.
3. Index greater than 0: accepted only while a message is open, with the same id, and the index is the next expected one. Then it is ACKed and delivered. Otherwise the fragment is out of sequence: no ACK, nothing delivered, no state change. The sender then retransmits and eventually abandons the message.

The ACK is sent before the payload is delivered. Empty fragments follow the same sequencing but deliver nothing.

## Firmware Update Workflow

A typical update sequence from the host side:

1. **Ping** -- verify connection
2. **GetInfo** -- read memory layout and protection level. Under read-out protection `Erase`, `Write` and `Finalize` answer `Protected` unless the device sets `allowUpdatesWhenReadProtected`
3. **Erase** -- erase firmware area (async, poll with `GetStatus` until `Idle`). Restarts the write stream and the AES chain
4. **Write** (repeated) -- stream firmware data in chunks. Only the final chunk may be shorter than the write alignment. Resend the same sequence after a lost response; the device does not write the data twice
5. **GetHash** -- verify SHA256 of the raw data sent (ciphertext in AES mode)
6. **Finalize** -- send the expected `FirmwareHeader` of the plaintext image: `crc32` (CRC32 of the image as stored in flash, i.e. after decryption) and `size` (image length), both little-endian, 8 bytes. The device checks the flash content against both values and answers `ChecksumMismatch` without writing the header if they differ
7. **Verify** -- confirm firmware integrity
8. **Start** or **Reset** -- launch new firmware. The device answers first and acts about 150 ms later

Legacy hosts that cannot compute the CRC can send an empty `Finalize` payload when the device has `requireFinalizeChecksum = false`. That writes the header without a checksum check, so the integrity check is lost. Keep `requireFinalizeChecksum = true` unless every host is updated.

### Retrying after a lost response

A command whose response is lost can be sent again. Whether that is safe depends on the command:

| Command | Safe to repeat | How |
|-|-|-|
| `Write` | Yes | Resend the same sequence. A duplicate sequence is answered without writing the data twice |
| `Read` | Yes, offset form only | Use the offset form (length 4) with the same offset. The streaming form (length 0) is not retry-safe: it advances the read position |
| `Finalize` | Yes | Send the same header again. The device answers `Success` without rewriting an identical stored header |
| `Erase` | Yes, but it restarts the upload | A repeated `Erase` restarts the write stream. Poll `GetStatus` until `Idle`, then send the whole image again |
| `Ping`, `GetInfo`, `GetStatus`, `GetHash`, `Verify` | Yes | Idempotent. Send again |
| `ResetWritePos`, `ResetReadPos` | Yes | Idempotent as long as no `Write` was accepted in between. A repeated `ResetWritePos` after a `Write` discards that data |

## API Reference

### IBootloader Enums

**Command** -- `uint8` command codes (see [Commands](#commands) table).

**BootloaderStatus** -- `uint8` response codes (see [Response Statuses](#response-statuses) table).

**State** -- `uint8` values sent by `GetStatus`: `Idle = 0`, `Receiving = 1`, `Processing = 2`, `Error = 3` (see [States](#states)).

**ResultStatus** -- general result type of the virtual methods and of `ICommunication` (for example `ok`, `inProgress`, `busy`, `error`, `invalidParameter`, `accessError`, `notSupported`).

### IBootloader Structs

| Struct | Fields | Description |
|-|-|-|
| `FirmwareHeader` | `crc32`, `size` | 8-byte firmware validation header (little-endian in the `Finalize` payload) |
| `CommandPacket` | `header`, `sequence`, `command`, `length`, `data[PacketDataMaxSize]` | Incoming command |
| `ResponsePacket` | `header`, `sequence`, `status`, `length`, `data[PacketDataMaxSize]` | Outgoing response |

### GetStatus Response

`GetStatus` answers 8 bytes. The layout is a packed struct with all bytes zero-initialised, so no stack contents leak:

| Offset | Size | Field |
|-|-|-|
| 0 | 1 | `state` (`State` value) |
| 1 | 3 | `reserved` (always `0`) |
| 4 | 4 | `bytesReceived` (little-endian on the ARM targets) |

`bytesReceived` counts the bytes accepted by `Write` since the last `Erase` or `ResetWritePos`.
