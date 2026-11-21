# FLASHAdapter

Abstract adapter for internal flash memory. Provides read, write, erase operations and read protection management for the MCU's embedded flash.

Header: `#include <Adapter/FLASHAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class FLASHAdapter : public IAdapter;
```

## Parameters

```cpp
struct Parameters {
    uint32 startAddress;
    uint32 endAddress;
};
```

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `FlashProtectionLevel` | `Level0` (no protection), `Level1` (partial), `Level2` (permanent), `Unknown` | Read protection levels |

## API

### Access Control

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(const Parameters&)` | `ResultStatus` | Set flash address range |
| `Unlock(uint32 key1, uint32 key2)` | `ResultStatus` | Unlock flash for write/erase |
| `Lock()` | `ResultStatus` | Lock flash (prevent write/erase) |

### Read / Write / Erase

| Method | Return | Description |
|--------|--------|-------------|
| `Read(uint8* address)` | `Result<uint8>` | Read byte at address |
| `Write(uint16* address, uint16 data)` | `ResultStatus` | Write halfword |
| `WriteData(uint32* address, const void* data, size_t size)` | `ResultStatus` | Write arbitrary data block |
| `PageErase(uint8* address)` | `ResultStatus` | Erase a flash page |
| `SectorErase(uint32 sectorNumber)` | `ResultStatus` | Erase a flash sector |
| `MassErase()` | `ResultStatus` | Erase entire flash |

### Status

| Method | Return | Description |
|--------|--------|-------------|
| `GetStatus()` | `ResultStatus` | Get flash operation status |
| `ClearStatusFlags()` | `ResultStatus` | Clear error/status flags |

### Option Bytes

| Method | Return | Description |
|--------|--------|-------------|
| `ReadOptionBytes()` | `Result<uint32>` | Read option byte register |
| `WriteOptionBytes(uint32)` | `ResultStatus` | Write option bytes |
| `UnlockOptionBytes()` | `ResultStatus` | Unlock option bytes for modification |
| `LockOptionBytes()` | `ResultStatus` | Lock option bytes |
| `IsOptionBytesLocked()` | `bool` | Check if option bytes are locked |

### Read Protection

| Method | Return | Description |
|--------|--------|-------------|
| `IsReadProtected()` | `bool` | Check if flash is read-protected |
| `GetReadProtectionLevel()` | `uint8` | Get raw protection level value |
| `GetProtectionLevel()` | `FlashProtectionLevel` | Get protection as enum |
| `SetReadProtectionLevel(FlashProtectionLevel)` | `ResultStatus` | Set protection level |
| `DisableReadProtection()` | `ResultStatus` | Remove read protection (Level0) |

## Code Location and Timing

- On the STM32G4 adapter, erase and program are polled. The FLASH interrupt is not used, and each call returns when the operation is done or its timeout has passed.
- While a bank is erased or programmed, a read of that bank waits until the operation ends. On a single-bank part (STM32G431) every read of the flash waits, so the CPU stalls for up to one operation, and the interrupts wait with it. Nothing is corrupted. Code that calls `PageErase()`, `MassErase()` or `WriteData()` can therefore run from flash.
- Run that code from RAM only when the interrupt latency during the operation matters. On dual-bank parts, read-while-write runs the code from the other bank instead.
- The STM32G4 adapter bounds a double-word program at 10 ms and a page erase at 100 ms, and it fails the call past the bound. The bounds are not measured times. The times on a given board are to be measured.

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static AFLASH flash;
};

// BSP.cpp
AFLASH BSP::flash = { FLASH };
```

## Usage Example

```cpp
// Store calibration data in last flash page
BSP::flash.Unlock(FLASH_KEY1, FLASH_KEY2);
BSP::flash.PageErase(reinterpret_cast<uint8*>(0x0800F800));
BSP::flash.WriteData(
    reinterpret_cast<uint32*>(0x0800F800),
    &calibrationData,
    sizeof(calibrationData)
);
BSP::flash.Lock();
```

> **Warning:** `FlashProtectionLevel::Level2` is **permanent** and cannot be downgraded. Use with extreme caution.
