# CRC -- Generic CRC Calculation Library

A header-only, template-based C++ library for computing CRC checksums of arbitrary width (4-bit to 64-bit). Includes 40+ predefined standard CRC parameter sets and supports both direct (bitwise) and table-accelerated computation.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Core Concepts](#core-concepts)
  - [Parameters](#parameters)
  - [Lookup Table](#lookup-table)
- [API Reference](#api-reference)
  - [Crc::Calculate (direct)](#crccalculate-direct)
  - [Crc::Calculate (table-accelerated)](#crccalculate-table-accelerated)
  - [Crc::Calculate (multi-part / streaming)](#crccalculate-multi-part--streaming)
- [Predefined CRC Standards](#predefined-crc-standards)
- [Examples](#examples)

---

## Quick Start

```cpp
#include <Utilities/Checksum/CRC/Crc.h>

// Calculate CRC-32 of a data buffer
uint8_t data[] = { 0x01, 0x02, 0x03, 0x04 };
uint32_t crc = Crc::Calculate(data, sizeof(data), Crc::CRC_32());
```

---

## Core Concepts

### Parameters

`Crc::Parameters<CRCType, CRCWidth>` describes a CRC algorithm. Template arguments:

- `CRCType` -- unsigned integer type large enough to hold the CRC value (`uint8`, `uint16`, `uint32`, `uint64`).
- `CRCWidth` -- bit width of the CRC (e.g. `8`, `16`, `32`).

Fields:

| Field           | Type     | Description                                              |
|-----------------|----------|----------------------------------------------------------|
| `polynomial`    | CRCType  | Generator polynomial                                     |
| `initialValue`  | CRCType  | Initial CRC register value                               |
| `finalXOR`      | CRCType  | XOR mask applied to the final CRC                        |
| `reflectInput`  | bool     | Reflect (bit-reverse) each input byte before processing  |
| `reflectOutput` | bool     | Reflect the final CRC before applying `finalXOR`         |

You can define custom CRC parameters:

```cpp
// Custom CRC-16 definition
const Crc::Parameters<uint16, 16> myCustomCrc = {
    0x8005,  // polynomial
    0x0000,  // initialValue
    0x0000,  // finalXOR
    true,    // reflectInput
    true     // reflectOutput
};

uint16_t result = Crc::Calculate(data, size, myCustomCrc);
```

### Lookup Table

`Crc::Table<CRCType, CRCWidth>` precomputes a 256-entry lookup table for fast byte-at-a-time CRC calculation. Create it from a `Parameters` object:

```cpp
// Build table once, reuse many times
auto table = Crc::CRC_32().MakeTable();

uint32_t crc1 = Crc::Calculate(buf1, len1, table);
uint32_t crc2 = Crc::Calculate(buf2, len2, table);
```

The table stores 256 entries of type `CRCType` (e.g. 1 KB for CRC-32).

Table methods:

| Method              | Returns                             | Description                      |
|---------------------|-------------------------------------|----------------------------------|
| `GetParameters()`   | `const Parameters<CRCType, CRCWidth>&` | Returns the parameters used  |
| `GetTable()`        | `const CRCType*`                    | Raw pointer to the 256-entry LUT |
| `operator[](index)` | `CRCType`                           | Access a single table entry      |

---

## API Reference

### Crc::Calculate (direct)

Bitwise CRC computation -- no lookup table needed.

```cpp
template<typename CRCType, uint16 CRCWidth>
static CRCType Calculate(
    const void* data,
    size_t size,
    const Parameters<CRCType, CRCWidth>& parameters
);
```

### Crc::Calculate (table-accelerated)

Uses a precomputed lookup table for faster computation.

```cpp
template<typename CRCType, uint16 CRCWidth>
static CRCType Calculate(
    const void* data,
    size_t size,
    const Table<CRCType, CRCWidth>& lookupTable
);
```

### Crc::Calculate (multi-part / streaming)

Both the direct and table-accelerated variants have an overload that accepts a previous CRC value, allowing incremental (multi-part) computation:

```cpp
// Direct, multi-part
template<typename CRCType, uint16 CRCWidth>
static CRCType Calculate(
    const void* data,
    size_t size,
    const Parameters<CRCType, CRCWidth>& parameters,
    CRCType previousCrc
);

// Table-accelerated, multi-part
template<typename CRCType, uint16 CRCWidth>
static CRCType Calculate(
    const void* data,
    size_t size,
    const Table<CRCType, CRCWidth>& lookupTable,
    CRCType previousCrc
);
```

---

## Predefined CRC Standards

All accessors are static methods on `Crc` that return `const Parameters<...>&`.

| Method                  | Width | Type     | Polynomial   |
|-------------------------|-------|----------|--------------|
| `CRC_4_ITU()`           | 4     | uint8    | 0x3          |
| `CRC_5_EPC()`           | 5     | uint8    | 0x09         |
| `CRC_5_ITU()`           | 5     | uint8    | 0x15         |
| `CRC_5_USB()`           | 5     | uint8    | 0x05         |
| `CRC_6_CDMA2000A()`     | 6     | uint8    | 0x27         |
| `CRC_6_CDMA2000B()`     | 6     | uint8    | 0x07         |
| `CRC_6_ITU()`           | 6     | uint8    | 0x03         |
| `CRC_7()`               | 7     | uint8    | 0x09         |
| `CRC_8()`               | 8     | uint8    | 0x07         |
| `CRC_8_EBU()`           | 8     | uint8    | 0x1D         |
| `CRC_8_MAXIM()`         | 8     | uint8    | 0x31         |
| `CRC_8_WCDMA()`         | 8     | uint8    | 0x9B         |
| `CRC_10()`              | 10    | uint16   | 0x233        |
| `CRC_10_CDMA2000()`     | 10    | uint16   | 0x3D9        |
| `CRC_11()`              | 11    | uint16   | 0x385        |
| `CRC_12_CDMA2000()`     | 12    | uint16   | 0xF13        |
| `CRC_12_DECT()`         | 12    | uint16   | 0x80F        |
| `CRC_12_UMTS()`         | 12    | uint16   | 0x80F        |
| `CRC_13_BBC()`          | 13    | uint16   | 0x1CF5       |
| `CRC_15()`              | 15    | uint16   | 0x4599       |
| `CRC_15_MPT1327()`      | 15    | uint16   | 0x6815       |
| `CRC_16_ARC()`          | 16    | uint16   | 0x8005       |
| `CRC_16_BUYPASS()`      | 16    | uint16   | 0x8005       |
| `CRC_16_CCITTFALSE()`   | 16    | uint16   | 0x1021       |
| `CRC_16_CDMA2000()`     | 16    | uint16   | 0xC867       |
| `CRC_16_CMS()`          | 16    | uint16   | 0x8005       |
| `CRC_16_DECTR()`        | 16    | uint16   | 0x0589       |
| `CRC_16_DECTX()`        | 16    | uint16   | 0x0589       |
| `CRC_16_DNP()`          | 16    | uint16   | 0x3D65       |
| `CRC_16_GENIBUS()`      | 16    | uint16   | 0x1021       |
| `CRC_16_KERMIT()`       | 16    | uint16   | 0x1021       |
| `CRC_16_MAXIM()`        | 16    | uint16   | 0x8005       |
| `CRC_16_MODBUS()`       | 16    | uint16   | 0x8005       |
| `CRC_16_T10DIF()`       | 16    | uint16   | 0x8BB7       |
| `CRC_16_USB()`          | 16    | uint16   | 0x8005       |
| `CRC_16_X25()`          | 16    | uint16   | 0x1021       |
| `CRC_16_XMODEM()`       | 16    | uint16   | 0x1021       |
| `CRC_17_CAN()`          | 17    | uint32   | 0x1685B      |
| `CRC_21_CAN()`          | 21    | uint32   | 0x102899     |
| `CRC_24()`              | 24    | uint32   | 0x864CFB     |
| `CRC_24_FLEXRAYA()`     | 24    | uint32   | 0x5D6DCB     |
| `CRC_24_FLEXRAYB()`     | 24    | uint32   | 0x5D6DCB     |
| `CRC_30()`              | 30    | uint32   | 0x2030B9C7   |
| `CRC_32()`              | 32    | uint32   | 0x04C11DB7   |
| `CRC_32_BZIP2()`        | 32    | uint32   | 0x04C11DB7   |
| `CRC_32_C()`            | 32    | uint32   | 0x1EDC6F41   |
| `CRC_32_MPEG2()`        | 32    | uint32   | 0x04C11DB7   |
| `CRC_32_POSIX()`        | 32    | uint32   | 0x04C11DB7   |
| `CRC_32_Q()`            | 32    | uint32   | 0x814141AB   |
| `CRC_40_GSM()`          | 40    | uint64   | 0x0004820009 |
| `CRC_64()`              | 64    | uint64   | 0x42F0E1EBA9EA3693 |

---

## Examples

### Simple one-shot CRC-32

```cpp
const char* message = "Hello, World!";
uint32_t crc = Crc::Calculate(message, strlen(message), Crc::CRC_32());
```

### Table-accelerated CRC-16 Modbus

```cpp
// Create the table once (e.g. as a static or global)
static auto modbusTable = Crc::CRC_16_MODBUS().MakeTable();

uint8_t frame[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
uint16_t crc = Crc::Calculate(frame, sizeof(frame), modbusTable);
```

### Multi-part (streaming) computation

```cpp
// Compute CRC over data received in chunks
auto table = Crc::CRC_32().MakeTable();

uint8_t chunk1[] = { 0x01, 0x02 };
uint8_t chunk2[] = { 0x03, 0x04 };

uint32_t crc = Crc::Calculate(chunk1, sizeof(chunk1), table);
crc = Crc::Calculate(chunk2, sizeof(chunk2), table, crc);
// crc now equals Crc::Calculate over the full 4-byte sequence
```

### Custom CRC parameters

```cpp
const Crc::Parameters<uint16, 16> myProtocolCrc = {
    0xABCD,  // polynomial
    0xFFFF,  // initialValue
    0x0000,  // finalXOR
    false,   // reflectInput
    false    // reflectOutput
};

auto table = myProtocolCrc.MakeTable();
uint16_t crc = Crc::Calculate(data, length, table);
```

### CRC-8 for sensor communication

```cpp
uint8_t sensorData[] = { 0xBE, 0xEF };
uint8_t crc = Crc::Calculate(sensorData, sizeof(sensorData), Crc::CRC_8());
```
