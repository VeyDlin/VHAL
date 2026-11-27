# Checksum::Crc

A CRC of any width from 1 to 64 bits, computed by an engine the type chooses. The type is the parameters and the engine:
`Crc<Parameters, Engine>`. The parameters are the model of the reveng CRC catalogue. The default engine is `CrcBitwise`, which has
no table.

Headers: `<Utilities/Checksum/CRC/Crc.h>` (the type), `<Utilities/Checksum/CRC/CrcParameters.h>` (the parameters), and
`<Utilities/Checksum/CRC/Engine/...>` for each engine.

## One call

```cpp
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>

using Crc16Modbus = Checksum::Crc<Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x8005)
	.Initial(0xFFFF)
	.ReflectIn()
	.ReflectOut()
	.Check(0x4B37), Checksum::CrcTable>;

const uint16 crc = Crc16Modbus::Calculate(std::span<const uint8>(frame, length));
```

`Calculate` takes a `std::span<const uint8>`; there is no pointer form. The result type is the smallest unsigned type of the width:
`uint8` up to 8 bits, `uint16` up to 16, `uint32` up to 32, `uint64` above.

## Data in parts

A message can come in parts. An object holds the register between calls, and the CRC of the parts equals the CRC of the whole, at
any split, with empty parts allowed.

```cpp
Crc16Modbus crc;
crc.Add(header);
crc.Add(payload);
const uint16 value = crc.Get();  // the CRC so far; Add may follow
crc.Reset();                     // back to the initial register
```

`Get` does not change the register, so it can be called between parts.

## Parameters

`CrcParameters` is a chain. `Width` and `Polynomial` are required. The others default to 0 and false.

| Method | Meaning |
|---|---|
| `Width(bits)` | The width, 1 to 64. Required. |
| `Polynomial(value)` | The polynomial in normal form, without the leading term. Required. |
| `Initial(value)` | The initial value of the unreflected register. Default 0. |
| `FinalXor(value)` | XORed into the result last. Default 0. |
| `ReflectIn(bool = true)` | Input bytes are reflected (least significant bit first). Default false. |
| `ReflectOut(bool = true)` | The output is reflected before the final XOR. Default false. |
| `Check(value)` | The CRC of the ASCII text "123456789". Optional; when given, it must match. |

The initial value is always given unreflected, also for a reflected input: the library reflects it. A non-palindromic initial value
therefore gives the catalogue's CRC.

Every mistake is a compile error with its message:

| Message | Cause |
|---|---|
| CRC: the CRC parameters have no Width | `Width` is not in the chain |
| CRC: the CRC parameters have no Polynomial | `Polynomial` is not in the chain |
| CRC: a CRC is 1 to 64 bits wide | the width is 0 or above 64 |
| CRC: the polynomial of the CRC parameters is wider than the width | the polynomial has a bit at or above the width |
| CRC: the initial value of the CRC parameters is wider than the width | the same for `Initial` |
| CRC: the final XOR of the CRC parameters is wider than the width | the same for `FinalXor` |
| CRC: the check value of the CRC parameters does not match the CRC of 123456789 | the diagnostic lists the value the parameters give, in hexadecimal |
| CRC: the engine cannot compute this CRC | the engine's `Supports` says no for these parameters |

## Engines

Each engine is a template argument. The cost is measured by `.test/size/Size.cmake` on arm-none-eabi-g++ 15.2 at `-Os` and `-O2`, as the
bytes of text added to a program that makes the same calls with no CRC. RAM is 0 for every engine.

| Engine | Table | Widths | Algorithms: 1 (MODBUS), M4 / M0+ | Algorithms: 3 (MODBUS, ISO-HDLC, SMBUS), M4 / M0+ |
|---|---|---|---|---|
| `CrcBitwise` (default) | none | 1 to 64 | 24 / 24 | 112 / 116 |
| `CrcNibble` | 16 entries of the value type | 4 to 64 | 68 / 64 | 260 / 264 |
| `CrcTable` | 256 entries of the value type | 8 to 64 | 528 / 528 | 1876 / 1876 |

The value type is the result type of the width: `uint8` up to 8 bits, `uint16` up to 16, `uint32` up to 32, `uint64` above. A table
engine stores 256 entries of the value type per algorithm: 256, 512, 1024 or 2048 bytes of flash. A bitwise engine stores no table and
pays only its loop.

On a Cortex-M4 the bitwise engine takes eight steps of about five instructions per byte, the nibble engine two table lookups per byte,
and the table engine one.

At `-O2`, `CrcBitwise` costs 36 / 24 for one algorithm and 124 / 120 for three. `.test/size/Size.cmake` fails when a cost grows above its
limit, and it also measures `CrcBitwise` at `-O3`, where the compiler's CRC optimisation would otherwise replace the loop by a table.

### The engine contract

An engine is a struct with two static function templates on the parameters:

```cpp
struct MyEngine {
	template<Checksum::CrcParameters Parameters>
	static constexpr bool Supports();

	template<Checksum::CrcParameters Parameters>
	static constexpr Checksum::CrcRegister<Parameters.width> Run(
		Checksum::CrcRegister<Parameters.width> start, std::span<const uint8> data);
};
```

- `Supports` says whether the engine computes the parameters. `Crc` reports the answer as a compile error when it says no.
- `Run` takes the register and returns the register after the bytes. The register is the one of `CrcStart`:
  - for a reflected input (`ReflectIn`), the register is low-aligned: the CRC is in the low `Width` bits, reflected;
  - for a normal input, the register is top-aligned: the CRC is in the top `Width` bits of the register type, and the bits below are
    zero.
  The register type is `uint32` for widths up to 32 and `uint64` above.
- `Run` is called once per part, so the register must carry the whole state. Each engine is stateless.

A hardware engine (the STM32G4 CRC unit, for example) is written against the same contract: it takes the register from `CrcStart`,
feeds the bytes, and returns the register, which `CrcFinish` turns into the value.

## Presets

A preset is a name for a CRC: one file each under `Preset/`, in the namespace `Checksum`, with the catalogue name in PascalCase. Each
file is one `using` line, so a preset that is not used costs nothing: a program that includes all the preset files and uses one is the
same size as a program that includes that one file (`.test/size/Presets.cmake`). The check value of a preset is checked when the preset is
used, and by `.test/TestCrcPresets.cpp` for every preset with every engine that supports it.

Include the file of the preset, for example `<Utilities/Checksum/CRC/Preset/Crc16Modbus.h>`, and use the name:

```cpp
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>

const uint16 crc = Checksum::Crc16Modbus::Calculate(std::span<const uint8>(frame, length));
```

The table is sorted by width, then by name. The width, the parameters and the check value are those of the catalogue.

| File | Catalogue name | Width | Parameters | Check |
|---|---|---:|---|---:|
| `Crc4Itu.h` | CRC-4/ITU | 4 | poly 0x3, init 0x0, xorout 0x0, reflected | 0x7 |
| `Crc5Epc.h` | CRC-5/EPC | 5 | poly 0x09, init 0x09, xorout 0x00, not reflected | 0x00 |
| `Crc5Itu.h` | CRC-5/ITU | 5 | poly 0x15, init 0x00, xorout 0x00, reflected | 0x07 |
| `Crc5Usb.h` | CRC-5/USB | 5 | poly 0x05, init 0x1F, xorout 0x1F, reflected | 0x19 |
| `Crc6Cdma2000A.h` | CRC-6/CDMA2000-A | 6 | poly 0x27, init 0x3F, xorout 0x00, not reflected | 0x0D |
| `Crc6Cdma2000B.h` | CRC-6/CDMA2000-B | 6 | poly 0x07, init 0x3F, xorout 0x00, not reflected | 0x3B |
| `Crc6Itu.h` | CRC-6/ITU | 6 | poly 0x03, init 0x00, xorout 0x00, reflected | 0x06 |
| `Crc7Mmc.h` | CRC-7/MMC | 7 | poly 0x09, init 0x00, xorout 0x00, not reflected | 0x75 |
| `Crc8Ebu.h` | CRC-8/EBU | 8 | poly 0x1D, init 0xFF, xorout 0x00, reflected | 0x97 |
| `Crc8Maxim.h` | CRC-8/MAXIM-DOW | 8 | poly 0x31, init 0x00, xorout 0x00, reflected | 0xA1 |
| `Crc8Smbus.h` | CRC-8/SMBUS | 8 | poly 0x07, init 0x00, xorout 0x00, not reflected | 0xF4 |
| `Crc8Wcdma.h` | CRC-8/WCDMA | 8 | poly 0x9B, init 0x00, xorout 0x00, reflected | 0x25 |
| `Crc10.h` | CRC-10 | 10 | poly 0x233, init 0x000, xorout 0x000, not reflected | 0x199 |
| `Crc10Cdma2000.h` | CRC-10/CDMA2000 | 10 | poly 0x3D9, init 0x3FF, xorout 0x000, not reflected | 0x233 |
| `Crc11.h` | CRC-11 | 11 | poly 0x385, init 0x01A, xorout 0x000, not reflected | 0x5A3 |
| `Crc12Cdma2000.h` | CRC-12/CDMA2000 | 12 | poly 0xF13, init 0xFFF, xorout 0x000, not reflected | 0xD4D |
| `Crc12Dect.h` | CRC-12/DECT | 12 | poly 0x80F, init 0x000, xorout 0x000, not reflected | 0xF5B |
| `Crc12Umts.h` | CRC-12/UMTS | 12 | poly 0x80F, init 0x000, xorout 0x000, reflected out only | 0xDAF |
| `Crc13Bbc.h` | CRC-13/BBC | 13 | poly 0x1CF5, init 0x0000, xorout 0x0000, not reflected | 0x04FA |
| `Crc15Can.h` | CRC-15/CAN | 15 | poly 0x4599, init 0x0000, xorout 0x0000, not reflected | 0x059E |
| `Crc15Mpt1327.h` | CRC-15/MPT1327 | 15 | poly 0x6815, init 0x0000, xorout 0x0001, not reflected | 0x2566 |
| `Crc16Arc.h` | CRC-16/ARC | 16 | poly 0x8005, init 0x0000, xorout 0x0000, reflected | 0xBB3D |
| `Crc16Buypass.h` | CRC-16/BUYPASS | 16 | poly 0x8005, init 0x0000, xorout 0x0000, not reflected | 0xFEE8 |
| `Crc16CcittFalse.h` | CRC-16/CCITT-FALSE | 16 | poly 0x1021, init 0xFFFF, xorout 0x0000, not reflected | 0x29B1 |
| `Crc16Cdma2000.h` | CRC-16/CDMA2000 | 16 | poly 0xC867, init 0xFFFF, xorout 0x0000, not reflected | 0x4C06 |
| `Crc16Cms.h` | CRC-16/CMS | 16 | poly 0x8005, init 0xFFFF, xorout 0x0000, not reflected | 0xAEE7 |
| `Crc16DectR.h` | CRC-16/DECT-R | 16 | poly 0x0589, init 0x0000, xorout 0x0001, not reflected | 0x007E |
| `Crc16DectX.h` | CRC-16/DECT-X | 16 | poly 0x0589, init 0x0000, xorout 0x0000, not reflected | 0x007F |
| `Crc16Dnp.h` | CRC-16/DNP | 16 | poly 0x3D65, init 0x0000, xorout 0xFFFF, reflected | 0xEA82 |
| `Crc16Genibus.h` | CRC-16/GENIBUS | 16 | poly 0x1021, init 0xFFFF, xorout 0xFFFF, not reflected | 0xD64E |
| `Crc16IsoIec144433A.h` | CRC-16/ISO-IEC-14443-3-A | 16 | poly 0x1021, init 0xC6C6, xorout 0x0000, reflected | 0xBF05 |
| `Crc16Kermit.h` | CRC-16/KERMIT | 16 | poly 0x1021, init 0x0000, xorout 0x0000, reflected | 0x2189 |
| `Crc16Maxim.h` | CRC-16/MAXIM-DOW | 16 | poly 0x8005, init 0x0000, xorout 0xFFFF, reflected | 0x44C2 |
| `Crc16Modbus.h` | CRC-16/MODBUS | 16 | poly 0x8005, init 0xFFFF, xorout 0x0000, reflected | 0x4B37 |
| `Crc16Riello.h` | CRC-16/RIELLO | 16 | poly 0x1021, init 0xB2AA, xorout 0x0000, reflected | 0x63D0 |
| `Crc16T10Dif.h` | CRC-16/T10-DIF | 16 | poly 0x8BB7, init 0x0000, xorout 0x0000, not reflected | 0xD0DB |
| `Crc16Tms37157.h` | CRC-16/TMS37157 | 16 | poly 0x1021, init 0x89EC, xorout 0x0000, reflected | 0x26B1 |
| `Crc16Usb.h` | CRC-16/USB | 16 | poly 0x8005, init 0xFFFF, xorout 0xFFFF, reflected | 0xB4C8 |
| `Crc16X25.h` | CRC-16/X-25 | 16 | poly 0x1021, init 0xFFFF, xorout 0xFFFF, reflected | 0x906E |
| `Crc16Xmodem.h` | CRC-16/XMODEM | 16 | poly 0x1021, init 0x0000, xorout 0x0000, not reflected | 0x31C3 |
| `Crc17Can.h` | CRC-17/CAN-FD | 17 | poly 0x1685B, init 0x00000, xorout 0x00000, not reflected | 0x04F03 |
| `Crc21Can.h` | CRC-21/CAN-FD | 21 | poly 0x102899, init 0x000000, xorout 0x000000, not reflected | 0x0ED841 |
| `Crc24FlexrayA.h` | CRC-24/FLEXRAY-A | 24 | poly 0x5D6DCB, init 0xFEDCBA, xorout 0x000000, not reflected | 0x7979BD |
| `Crc24FlexrayB.h` | CRC-24/FLEXRAY-B | 24 | poly 0x5D6DCB, init 0xABCDEF, xorout 0x000000, not reflected | 0x1F23B8 |
| `Crc24OpenPgp.h` | CRC-24/OPENPGP | 24 | poly 0x864CFB, init 0xB704CE, xorout 0x000000, not reflected | 0x21CF02 |
| `Crc30Cdma.h` | CRC-30/CDMA | 30 | poly 0x2030B9C7, init 0x3FFFFFFF, xorout 0x3FFFFFFF, not reflected | 0x04C34ABF |
| `Crc32Bzip2.h` | CRC-32/BZIP2 | 32 | poly 0x04C11DB7, init 0xFFFFFFFF, xorout 0xFFFFFFFF, not reflected | 0xFC891918 |
| `Crc32C.h` | CRC-32C | 32 | poly 0x1EDC6F41, init 0xFFFFFFFF, xorout 0xFFFFFFFF, reflected | 0xE3069283 |
| `Crc32IsoHdlc.h` | CRC-32/ISO-HDLC | 32 | poly 0x04C11DB7, init 0xFFFFFFFF, xorout 0xFFFFFFFF, reflected | 0xCBF43926 |
| `Crc32Mpeg2.h` | CRC-32/MPEG-2 | 32 | poly 0x04C11DB7, init 0xFFFFFFFF, xorout 0x00000000, not reflected | 0x0376E6E7 |
| `Crc32Posix.h` | CRC-32/POSIX | 32 | poly 0x04C11DB7, init 0x00000000, xorout 0xFFFFFFFF, not reflected | 0x765E7680 |
| `Crc32Q.h` | CRC-32Q | 32 | poly 0x814141AB, init 0x00000000, xorout 0x00000000, not reflected | 0x3010BF7F |
| `Crc40Gsm.h` | CRC-40/GSM | 40 | poly 0x0004820009, init 0x0000000000, xorout 0xFFFFFFFFFF, not reflected | 0xD4164FC646 |
| `Crc64Ecma182.h` | CRC-64/ECMA-182 | 64 | poly 0x42F0E1EBA9EA3693, init 0x0000000000000000, xorout 0x0000000000000000, not reflected | 0x6C40DF5F0B497347 |

"reflected" means the input and the output are reflected; "reflected out only" means only the output is reflected.

## Own parameters

Any algorithm is a `CrcParameters` chain, as in the example at the top. Check values are optional, and when one is given it is checked
at compile time.
