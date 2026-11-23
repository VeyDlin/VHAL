# VUB::CrcCore

The CRC parameters and the plain functions both CRC engines and the transform use. None of them depends on the width or the
preset, so presets share one copy of the code.

## VUB::CrcParameters

A CRC in the model of the reveng CRC catalogue:

| Field | Meaning |
|---|---|
| `width` | 8, 16 or 32 |
| `polynomial` | the generator, in normal form |
| `initialValue` | the initial value as the unreflected register, also when `reflectIn` is set |
| `finalXor` | XORed into the result last |
| `reflectIn` | each input byte is reflected (the low bit first) |
| `reflectOut` | the result is reflected |
| `check` | the CRC of the ASCII string `"123456789"` in the catalogue, when `hasCheck` is set |
| `hasWidth`, `hasPolynomial`, `hasCheck` | set by the chain methods `Width`, `Polynomial` and `Check`; a transform refuses a chain without `Width` or `Polynomial` |

The parameters are written as a chain, for example `VUB::CrcParameters().Width(16).Polynomial(0x1021).Initial(0x1D0F).FinalXor(0x0000).ReflectIn(false).ReflectOut(false).Check(0xE5CC)`. `Initial` and `FinalXor` take a value; `ReflectIn` and `ReflectOut` take a bool that defaults to true. A brace list does not give the parameters.

`VUB::CrcAlgorithm<parameters>` gives the parameters as a type, which is what an engine's `Run` and `Supports` take.

## VUB::CrcOrder

The byte order of the CRC on the wire: `lowByteFirst` (the default) or `highByteFirst`. It is a type parameter of the transform,
so the transform holds no data for it.

## Functions

- `VUB::CrcStart(parameters)`: the register before the first byte. A reflected input starts from the initial value reflected, so the
  initial value keeps its catalogue meaning.
- `VUB::CrcFinish(parameters, remainder)`: reflects the register when `reflectIn` and `reflectOut` differ, then applies the final XOR.
- `VUB::CrcBitRun(width, polynomial, reflected, start, data)`: the bit loop. Used by the bitwise engine, and by the check value.
- `VUB::CrcCheckValue(parameters)`: the CRC of `"123456789"`, at compile time.
- `VUB::CrcFingerprint(parameters, order)`: a hash of the parameters and the byte order, so the configuration fingerprint tells two CRCs
  apart. The engine is not part of it.
- `VUB::CrcMask`, `VUB::CrcReverse`: the width mask and the bit reversal.

All are `constexpr` and take no state.
