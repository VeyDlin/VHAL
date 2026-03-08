# RegisterMap

A C++ header-only library for managing typed, address-mapped register banks with access control, read/write event hooks, and compile-time validation. Designed for embedded systems (VHAL framework).

---

## Table of Contents

- [Quick Start](#quick-start)
- [IRegisterData](#iregisterdata)
- [IRegisterMap](#iregistermap)
- [RegisterData](#registerdata)
- [RegisterDataMutator](#registerdatamutator)
- [RegisterDataFloat](#registerdatafloat)
- [RegisterMap](#registermap-class)
- [RegisterMapChecker](#registermapchecker)
- [API Reference](#api-reference)

---

## Quick Start

```cpp
#include <Utilities/Data/RegisterMap/RegisterMap.h>
#include <Utilities/Data/RegisterMap/Checker/RegisterMapChecker.h>

// Define register addresses (enum or constants)
enum Reg : uint32 {
    REG_STATUS  = 0x00,
    REG_COMMAND = 0x01,
    REG_VALUE   = 0x02,
};

// Declare individual registers with <Address, DataType>
RegisterData<REG_STATUS,  uint8>  regStatus;
RegisterData<REG_COMMAND, uint8>  regCommand;
RegisterData<REG_VALUE,   uint16> regValue;

// Create the map: <AddressType, MapSize (bytes), BufferSize (bytes)>
RegisterMap<uint8, 4, 2> myMap;

// Compile-time validation
using MyMapChecker = RegisterMapChecker<
    decltype(myMap),
    decltype(regStatus), decltype(regCommand), decltype(regValue)
>;

void Init() {
    // Link registers to the map
    myMap.RegisterData(regStatus, regCommand, regValue);

    // Configure access
    regStatus.ReadOnly();

    // Set write event callback
    regCommand.SetEvents([](const uint8& cmd) -> bool {
        // Return false to reject the write
        return cmd < 10;
    });
}

void Usage() {
    // Write using Set() or operator=
    regValue.Set(1234);
    regValue = 5678;

    // Read using Get() or implicit conversion
    uint16 v = regValue.Get();
    uint16 v2 = regValue; // implicit cast
}
```

---

## IRegisterData

Abstract base class for all register entries. Provides the interface that `RegisterMap` uses internally.

| Member | Description |
|---|---|
| `bool write` | Write access flag (default `true`) |
| `bool read` | Read access flag (default `true`) |
| `void LinkToMap(IRegisterMap&)` | Binds this register to a map instance |
| `virtual size_t DataTypeSize()` | Size of the stored data type in bytes |
| `virtual uint32 Addres()` | Returns the register address |
| `virtual bool WriteEvent(const uint8* buffer)` | Called before a write is committed; return `false` to reject |
| `virtual bool CanOnWrite()` | Whether an `onWrite` callback is set |
| `virtual bool CanOnRead()` | Whether an `onRead` callback is set |
| `virtual size_t GetUnsafe(uint8* outData)` | Copies register data to the output buffer |

---

## IRegisterMap

Abstract base class for the map container. Exposes registration of data entries and memory read/write.

```cpp
// Register multiple entries at once (variadic, fold-expression)
myMap.RegisterData(reg1, reg2, reg3);
```

All arguments must derive from `IRegisterData` (enforced via `static_assert`).

| Member | Description |
|---|---|
| `virtual size_t GetUnsafe(uint32 address, uint8* outData)` | Read raw bytes from a register by address |
| `virtual bool UpdateMemory(uint32 address, const uint8* buffer, size_t length)` | Write raw bytes to a register by address |
| `void RegisterData(Args&... args)` | Link one or more `IRegisterData` instances to this map |

---

## RegisterData

```cpp
template <uint32 Address, typename RawDataType>
class RegisterData : public IRegisterData;
```

A typed register at a fixed address. This is the primary class you interact with.

### Access Control

```cpp
RegisterData<0x00, uint16> reg;

reg.SetAccess(true, true);  // write=true, read=true
reg.ReadOnly();              // write=false, read=true
reg.WriteOnly();             // write=true, read=false
```

All setters return `*this` for chaining.

### Event Callbacks

```cpp
reg.SetEvents(
    // onWrite: called before write commits. Return false to reject.
    [](const uint16& newValue) -> bool {
        return newValue <= 1000; // reject values > 1000
    },
    // onRead: transform data before it is returned to the reader.
    [](const uint16& storedValue) -> uint16 {
        return storedValue * 2; // double on read
    }
);
```

Both callbacks are optional (pass `nullptr` to skip).

### Read / Write

```cpp
reg.Set(42);          // write value
reg = 42;             // same via operator=

uint16 val = reg.Get();  // read value
uint16 val2 = reg;       // same via implicit conversion
```

### Compile-Time Queries

```cpp
constexpr size_t size = RegisterData<0x00, uint16>::GetDataTypeSize(); // 2
constexpr uint32 addr = RegisterData<0x00, uint16>::GetAddress();      // 0x00
```

---

## RegisterDataMutator

```cpp
template <uint32 Address, typename DataType, typename RawDataType>
class RegisterDataMutator : public RegisterData<Address, RawDataType>;
```

Adds a conversion layer between a user-facing `DataType` and the underlying `RawDataType` stored in memory. Useful when the stored format differs from the API type.

```cpp
// Store as int16, expose as float via custom mutators
RegisterDataMutator<0x00, float, int16_t> reg(
    // readMutator: int16 -> float
    [](const int16_t& raw) -> float {
        return static_cast<float>(raw) / 100.0f;
    },
    // writeMutator: float -> int16
    [](const float& val) -> int16_t {
        return static_cast<int16_t>(val * 100.0f);
    }
);

reg = 3.14f;          // stored as 314 (int16)
float v = reg.Get();  // returns 3.14f
```

---

## RegisterDataFloat

```cpp
template <uint32 Address, typename DataType = int16_t, uint32 Accuracy = 100>
class RegisterDataFloat : public RegisterDataMutator<Address, float, DataType>;
```

A convenience specialization of `RegisterDataMutator` that stores a float as a fixed-point integer. The `Accuracy` template parameter defines the scaling factor.

```cpp
// Store temperature as int16, scaled by 100 (2 decimal digits)
RegisterDataFloat<0x00, int16_t, 100> temperature;

temperature = 23.45f;        // stored as 2345
float t = temperature.Get(); // returns 23.45f

// Higher precision: scale by 1000
RegisterDataFloat<0x02, int16_t, 1000> precise;
precise = 1.234f;            // stored as 1234
```

---

## RegisterMap Class

```cpp
template <typename AddressType, size_t MapSize, size_t BufferSize>
class RegisterMap : public IRegisterMap;
```

The concrete map implementation. Holds a flat byte array of `MapSize` bytes. Registers are laid out sequentially in the order they are linked.

| Template Parameter | Description |
|---|---|
| `AddressType` | Type used for addressing (e.g., `uint8`, `uint16`) |
| `MapSize` | Total size of the internal memory array in bytes |
| `BufferSize` | Size of the auxiliary buffer (for future partial updates) |

### Reading and Writing by Address

```cpp
RegisterMap<uint8, 8, 4> myMap;

// Raw read
uint8 buf[2];
size_t bytesRead = myMap.GetUnsafe(REG_VALUE, buf);

// Raw write
uint16 val = 0x1234;
bool ok = myMap.UpdateMemory(REG_VALUE, reinterpret_cast<const uint8*>(&val), sizeof(val));
```

`UpdateMemory` enforces access control, triggers `WriteEvent`, and copies data inside a critical section.

### Static Queries

```cpp
constexpr size_t addrSize = decltype(myMap)::GetAddressSize(); // sizeof(uint8)
constexpr size_t mapSize  = decltype(myMap)::GetMapSize();     // 8
constexpr size_t bufSize  = decltype(myMap)::GetBufferSize();  // 4
```

---

## RegisterMapChecker

Compile-time validators that catch configuration errors via `static_assert`. Located in `Checker/RegisterMapChecker.h`.

### RegisterMapOverlapChecker

Ensures no two registers occupy overlapping address ranges.

```cpp
// Fails at compile time if regA and regB overlap
using Check = RegisterMapOverlapChecker<
    decltype(myMap),
    decltype(regA), decltype(regB)
>;
```

### RegisterMapChecker

Extends `RegisterMapOverlapChecker` with additional checks:

- Total size of all registers does not exceed `MapSize`.
- No single register exceeds `BufferSize`.
- No register extends past the end of the map.

```cpp
using Check = RegisterMapChecker<
    decltype(myMap),
    decltype(reg1), decltype(reg2), decltype(reg3)
>;
```

### RegisterMapCheckerStrong

Extends `RegisterMapChecker` with strict equality constraints:

- Total register size must exactly equal `MapSize`.
- Every register's size must exactly equal `BufferSize`.

```cpp
// Strict: all registers must be same size, map fully packed
using Check = RegisterMapCheckerStrong<
    decltype(myMap),
    decltype(reg1), decltype(reg2)
>;
```

---

## API Reference

### RegisterData&lt;Address, RawDataType&gt;

| Method | Return | Description |
|---|---|---|
| `Get()` | `RawDataType` | Read the current value |
| `Set(const RawDataType&)` | `bool` | Write a value; returns `false` on failure |
| `operator=(const RawDataType&)` | `RegisterData&` | Write via assignment |
| `operator RawDataType()` | `RawDataType` | Implicit read conversion |
| `SetEvents(onWrite, onRead)` | `RegisterData&` | Set write/read callbacks |
| `SetAccess(write, read)` | `RegisterData&` | Set access flags |
| `ReadOnly()` | `RegisterData&` | Shortcut for write=false, read=true |
| `WriteOnly()` | `RegisterData&` | Shortcut for write=true, read=false |
| `static GetDataTypeSize()` | `size_t` | `sizeof(RawDataType)` (constexpr) |
| `static GetAddress()` | `uint32` | Template address (constexpr) |

### RegisterMap&lt;AddressType, MapSize, BufferSize&gt;

| Method | Return | Description |
|---|---|---|
| `RegisterData(args...)` | `void` | Link register entries to this map |
| `GetUnsafe(address, outData)` | `size_t` | Read raw register data by address |
| `UpdateMemory(address, buffer, length)` | `bool` | Write raw data by address |
| `static GetAddressSize()` | `size_t` | `sizeof(AddressType)` (constexpr) |
| `static GetMapSize()` | `size_t` | Map memory size (constexpr) |
| `static GetBufferSize()` | `size_t` | Buffer size (constexpr) |
