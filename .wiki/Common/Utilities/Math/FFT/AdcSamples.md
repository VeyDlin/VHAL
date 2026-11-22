# AdcSamples

A view over the raw codes of an ADC, such as the buffer that a DMA transfer fills. The FFT converts each code while it loads the sample, so no converted copy of the frame exists. The header is `Common/Utilities/Math/FFT/AdcSamples.h`.

## Table of Contents

- [Declaration](#declaration)
- [Scale](#scale)
- [Example](#example)
- [Conversion](#conversion)
- [Cost](#cost)
- [Rules](#rules)

## Declaration

```cpp
template <typename Number, uint32 Bits>
class AdcSamples;

AdcSamples(const uint16 *codes, int32 midCode = unitCodes);
```

- `Number` is `float`, `double` or `IQ<Q>`, and it must be the transform's `Number`.
- `Bits` is the resolution of the ADC, from 2 to 16. The codes are `uint16` values below `2^Bits`.
- `codes` is a pointer, not a copy. It must hold every sample the transform reads, and it must stay valid while the transform runs.
- `midCode` is the code that means zero. It defaults to `unitCodes`, which is `2^(Bits - 1)`, the middle of the range.

`AdcSamples<iq24, 12> samples(adcBuffer);` gives a 12-bit ADC with its zero at code 2048.

## Scale

Sample `i` is the code minus the mid code, divided by `unitCodes = 2^(Bits - 1)`. So full scale maps to 1.0 and the mid code to zero:

| Code (12 bits, mid 2048) | Sample |
|---|---|
| 0 | -1.0 |
| 1024 | -0.5 |
| 2048 | 0 |
| 3072 | 0.5 |
| 4095 | 1 - 2^-11, which is 0.99951171875 |

The top code does not reach 1.0, because the mid code is zero and the codes split unevenly around it: 2048 codes are at or below the mid code, and only 2047 are above it.

## Example

```cpp
#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>

static uint16 adcBuffer[1024];     // filled by the ADC DMA
static RealFFT<1024, iq24> fft;
static iq24 amplitude[RealFFT<1024, iq24>::binCount];

void Analyze() {
    AdcSamples<iq24, 12> samples(adcBuffer);
    fft.Analyze(samples, amplitude);
}
```

## Conversion

- **float and double** compute `(code - mid)` times `1 / unitCodes`. The reciprocal is a power of two, so the product is exact and the value is the same as a division.
- **IQ<Q>** computes the raw value in integers: `(code - mid) * 2^Q / 2^(Bits - 1)`. The offset is scaled before the value is formed, so the range of the IQ type does not matter for the code. For example, `IQ<24>` holds only values from -128 to 128, but a 12-bit code with its offset gives a raw value of at most `2^24`, which is exact.
- When `Q` is below `Bits - 1`, the scaled value is rounded to the nearest raw value, with ties toward plus infinity. This is the rounding of an IQ multiplication. For example, with `IQ<8>` and 12-bit codes, an offset of 4 codes gives raw 1, and an offset of -4 gives raw 0.

## Cost

An `AdcSamples` holds a pointer and the mid code. It needs no RAM beyond that, and no flash beyond the conversion code, which is a few instructions per sample. The alternative, a converted frame, costs `Size * sizeof(Number)` of RAM more: 1024 bytes for a 256-point float transform, 4096 bytes for a 1024-point `iq24` transform. The raw codes are needed in both cases.

## Rules

- A code above `2^Bits - 1` is not masked. The ADC must deliver codes in range.
- The view reads the codes while the transform runs. A DMA transfer that rewrites the buffer during the read gives a mix of two frames. Use a double buffer when the transfer runs continuously, or wait for the transfer to finish.
