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
  - [Virtual Methods to Implement](#virtual-methods-to-implement)
- [FLASHBootloader -- Flash Memory Bootloader](#flashbootloader----flash-memory-bootloader)
- [AESFLASHBootloader -- Encrypted Bootloader](#aesflashbootloader----encrypted-bootloader)
- [NRF8001Communication -- BLE Transport](#nrf8001communication----ble-transport)
- [Firmware Update Workflow](#firmware-update-workflow)
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

    Status::statusType Initialize() override { return uart.Initialize(); }
    Status::statusType SendData(std::span<const uint8> data) override {
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

    Status::statusType OnEraseIteration(
        uint32 address, uint32 endAddress,
        uint32 iteration, uint32& bytesErased
    ) override {
        uint32 sector = GetSectorFromAddress(address);
        uint32 sectorSize = GetSectorSize(sector);
        flashAdapter.EraseSector(sector);
        bytesErased = sectorSize;
        return (address + sectorSize >= endAddress) ? Status::ok : Status::inProgress;
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

    virtual Status::statusType Initialize() = 0;
    virtual Status::statusType SendData(std::span<const uint8> data) = 0;
    virtual bool IsReadyToSend() const = 0;
    virtual bool IsConnected() const = 0;
    virtual void SetDataReceivedCallback(DataReceivedCallback callback) = 0;
    virtual size_t GetMaxPacketSize() const = 0;
    virtual void Process() {}  // Default no-op; override for polling transports
};
```

Key contract:
- `Process()` must be non-blocking (< 10 ms).
- `DataReceivedCallback` should only be called with valid, CRC-checked data.
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
| `Erase` | `0x10` | Erases entire firmware area (async) |
| `Write` | `0x11` | Streams firmware data (auto-increments write position) |
| `Read` | `0x12` | Reads firmware data back (auto-increments read position) |
| `Verify` | `0x13` | Validates firmware CRC32 |
| `Finalize` | `0x14` | Writes firmware header with CRC32 and size |
| `ResetWritePos` | `0x15` | Resets write position to 0 |
| `ResetReadPos` | `0x16` | Resets read position to 0 |
| `GetHash` | `0x17` | Returns SHA256 hash of all raw data sent so far |
| `Start` | `0x20` | Launches the application |
| `Reset` | `0x21` | Resets the device (150 ms delay) |
| `GetStatus` | `0x30` | Returns current state and bytes received |
| `UnlockDevice` | `0x40` | Removes read protection (requires password) |
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
| `Error` | `0x01` | General error |
| `InvalidCommand` | `0x02` | Unknown command |
| `InvalidParameters` | `0x03` | Bad parameters |
| `Busy` | `0x04` | Device busy (e.g., erasing) |
| `NotReady` | `0x05` | Not ready |
| `Protected` | `0x06` | Memory protected |
| `CryptoError` | `0x07` | Encryption/decryption failure |
| `AccessDenied` | `0x08` | Blocked by RDP Level 2 |
| `EndOfData` | `0x09` | No more data to read |

### Firmware Header

Stored at the **end** of the firmware memory region (`startAddress + memorySize - 8`):

```cpp
struct FirmwareHeader {
    uint32 crc32;   // CRC32 of all firmware data
    uint32 size;    // Firmware size in bytes
};
```

### States

| State | Description |
|-|-|
| `Idle` | Waiting for commands |
| `Receiving` | Receiving data |
| `Processing` | Executing a command (sends `Busy` to new requests) |
| `Error` | Error state |

### Virtual Methods to Implement

**Required (pure virtual):**

| Method | Purpose |
|-|-|
| `OnCheckBootloaderRequest()` | Return `true` to enter bootloader, `false` to start app |
| `OnStartApplication()` | Jump to application (deinit peripherals, set VTOR, set MSP) |
| `GetMemoryStartAddress()` | Application memory start address |
| `GetMemorySize()` | Application memory size |
| `OnWriteMemory(addr, data)` | Write data to memory |
| `OnReadMemory(addr, data)` | Read data from memory |
| `OnEraseIteration(addr, end, iter, &erased)` | Erase one sector/page per call |

**Optional (with defaults):**

| Method | Default | Purpose |
|-|-|-|
| `OnBeforeInitialize()` | No-op | Early init (e.g., Flash setup) |
| `OnCheckAccessLevel()` | Allow | Check RDP protection level |
| `OnInitialize()` | No-op | Post-check initialization |
| `OnProcess()` | Runs async ops | Main loop hook |
| `GetProtectionLevel()` | `nullopt` | Return RDP level for `GetInfo` |
| `IsMemoryProtected()` | `false` | Check if memory is read-protected |
| `OnEncryptData(data)` | Passthrough | Encrypt data for Read responses |
| `OnDecryptData(data)` | Passthrough | Decrypt data from Write commands |
| `OnHasEnoughData(buffer)` | `size > 0` | Check if buffer has enough for processing |
| `OnGetRequiredBytes(buffer)` | All | How many bytes to consume |
| `OnFinalizeFirmware()` | Write header | Create CRC32 + size header |
| `OnUnlockDevice(password)` | Not supported | Remove read protection |

### Public API

```cpp
// Initialize bootloader (checks boot request, starts communication)
Status::statusType Initialize();

// Main loop -- call repeatedly
void Process();

// Attempt to start the application
bool StartApplication();

// Register a handler for UserData commands
void SetUserDataHandler(UserDataHandler handler);

// Set bootloader version reported by GetInfo
void SetBootloaderVersion(uint64 version);
```

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

Extends `IBootloader` with `AFLASH` adapter integration. Provides concrete implementations of `OnWriteMemory`, `OnReadMemory`, `OnCheckAccessLevel`, `OnUnlockDevice`, and memory parameter methods.

```cpp
template<size_t RxBufferSize = 1024, size_t AccumBufferSize = 512, size_t PacketDataMaxSize = 240>
class FLASHBootloader : public IBootloader<RxBufferSize, AccumBufferSize, PacketDataMaxSize>;
```

Constructor:

```cpp
FLASHBootloader(ICommunication& comm, AFLASH& flash, uint32 startAddr, uint32 size);
```

**Still requires implementation of:**
- `OnCheckBootloaderRequest()`
- `OnStartApplication()`
- `OnEraseIteration(...)`

Protection levels from the Flash adapter are mapped automatically:
- `Level0` -- no protection, full access
- `Level1` -- limited protection, bootloader can operate
- `Level2` -- permanent protection, returns `AccessDenied`

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
bootloader.SetIV(newIV);          // Set new IV and reset CBC chain state
auto currentIV = bootloader.GetIV();
auto mode = bootloader.GetMode(); // AESMode::ECB / CBC / CTR
constexpr auto ks = MyEncryptedBootloader::GetKeySize();
```

### Security Notes

- The destructor securely clears IV and internal buffers.
- Encryption/decryption errors return an empty span (never partial data).
- ECB/CBC modes require data aligned to 16-byte blocks; CTR works with any size.
- For CBC, the IV chains across consecutive decrypt calls within a session.

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

## Firmware Update Workflow

A typical update sequence from the host side:

1. **Ping** -- verify connection
2. **GetInfo** -- read memory layout, check protection level
3. **Erase** -- erase firmware area (async, poll with `GetStatus`)
4. **Write** (repeated) -- stream firmware data in chunks
5. **GetHash** -- verify SHA256 of transmitted data
6. **Finalize** -- write CRC32 + size header
7. **Verify** -- confirm firmware integrity
8. **Start** or **Reset** -- launch new firmware

## API Reference

### IBootloader Enums

**Command** -- `uint8` command codes (see [Commands](#commands) table).

**BootloaderStatus** -- `uint8` response codes (see [Response Statuses](#response-statuses) table).

### IBootloader Structs

| Struct | Fields | Description |
|-|-|-|
| `FirmwareHeader` | `crc32`, `size` | 8-byte firmware validation header |
| `EncryptedWriteHeader` | `address`, `realSize`, `paddedSize` | Encrypted write metadata |
| `CommandPacket` | `header`, `sequence`, `command`, `length`, `data[240]` | Incoming command |
| `ResponsePacket` | `header`, `sequence`, `status`, `length`, `data[240]` | Outgoing response |
