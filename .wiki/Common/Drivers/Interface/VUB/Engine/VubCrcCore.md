# VubCrcCore

The CRC parameters and the plain functions both CRC engines and the transform use. None of them depends on the width or the
preset, so presets share one copy of the code.

## VubCrcParameters

A CRC in the model of the reveng CRC catalogue:

| Field | Meaning |
|---|---|
| `width` | 8, 16 or 32 |
| `polynomial` | the generator, in normal form |
| `initialValue` | the initial value as the unreflected register, also when `reflectIn` is set |
| `finalXor` | XORed into the result last |
| `reflectIn` | each input byte is reflected (the low bit first) |
| `reflectOut` | the result is reflected |
| `check` | the CRC of the ASCII string `"123456789"` in the catalogue |

`VubCrcAlgorithm<parameters>` gives the parameters as a type, which is what an engine's `Run` and `Supports` take.

## VubCrcOrder

The byte order of the CRC on the wire: `lowByteFirst` (the default) or `highByteFirst`. It is a type parameter of the transform,
so the transform holds no data for it.

## Functions

- `VubCrcStart(parameters)`: the register before the first byte. A reflected input starts from the initial value reflected, so the
  initial value keeps its catalogue meaning.
- `VubCrcFinish(parameters, remainder)`: reflects the register when `reflectIn` and `reflectOut` differ, then applies the final XOR.
- `VubCrcBitRun(width, polynomial, reflected, start, data)`: the bit loop. Used by the bitwise engine, and by the check value.
- `VubCrcCheckValue(parameters)`: the CRC of `"123456789"`, at compile time.
- `VubCrcFingerprint(parameters, order)`: a hash of the parameters and the byte order, so the configuration fingerprint tells two CRCs
  apart. The engine is not part of it.
- `VubCrcMask`, `VubCrcReverse`: the width mask and the bit reversal.

All are `constexpr` and take no state.
