# FFT

A radix-2 Fast Fourier Transform of a real signal, for spectrum analysis on a microcontroller. The class is portable: it uses only the VHAL data types and the integer math of `IQ`, holds its packed work array, and allocates nothing. It runs the same on every MCU, with or without an FPU.

## Table of Contents

- [Quick Start](#quick-start)
- [How It Works](#how-it-works)
- [API Reference](#api-reference)
- [Sources](#sources)
- [Number Types and Scaling](#number-types-and-scaling)
- [Accuracy](#accuracy)
- [Sizes](#sizes)
- [Choosing the Size](#choosing-the-size)

## Quick Start

```cpp
#include <Utilities/Math/FFT/RealFFT.h>

// 512 samples at 16 kHz: a bin is 31.25 Hz wide, and the spectrum has 257 bins (DC to 8 kHz).
static RealFFT<512> fft;
float samples[512];
float amplitude[RealFFT<512>::binCount];

// Fill samples with the newest 512 values, scaled to -1..1.
fft.Analyze(samples, amplitude);

// amplitude[k] is the amplitude of a sine at bin k, so amplitude[k] == 0.5f for a 0.5 full-scale sine at a bin centre.
```

The same transform in integer math, with the samples and the spectrum in `IQ<24>`:

```cpp
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>

static RealFFT<1024, iq24> fft;
iq24 samples[1024];
iq24 amplitude[RealFFT<1024, iq24>::binCount];

fft.Analyze(samples, amplitude);
```

Keep the object static or in a long-lived structure. An instance holds only its packed work array: `Size / 2` complex values.

## How It Works

- A real signal of `Size` samples is packed into a complex signal of `Size / 2` points (even samples as the real part, odd samples as the imaginary part). The complex FFT of that signal is unpacked into the `Size / 2 + 1` bins of the real spectrum. Each bin is computed straight from the packed transform, so no spectrum buffer is needed.
- The window, the twiddle factors (the cosines and sines of the full circle, one table for every stage) and the bit-reversal permutation are `constexpr` tables. The compiler builds them, they are constant data in flash, and nothing is computed at start-up. The sines behind them are accurate over the whole circle.
- `Analyze` applies the window and returns the amplitude of each bin. With the Hann window, a sine of amplitude A at a bin centre reads as A. A sine between two bins reads as the Hann main lobe, which is about 0.85 of its amplitude at the nearest bin.
- `Forward` is the DFT of the signal without the window. Its bins are the DFT values, so Parseval's relation holds for them: the sum of the squared samples equals `(|X0|^2 + |X[Size/2]|^2 + 2 * sum |Xk|^2) / Size`. In `IQ` the bins are scaled (see [Number Types and Scaling](#number-types-and-scaling)).
- The DC and Nyquist bins have no mirror image, so a constant of value v reads as `2 v` in `Analyze` with the Hann window.

## API Reference

### Template Parameters

`template <uint32 Size, typename Number = float, typename Window = HannWindow>`

- `Size`: the number of samples, a power of two from 4 to 2^17. A static assertion rejects other values.
- `Number`: `float`, `double` or `IQ<Q>` with Q up to 29 (for example `iq24`). `IQ<30>` fails to compile, because a spectrum bin of a full-scale input can reach 2, which `IQ<30>` cannot hold. Any other type fails to compile.
- `Window`: `HannWindow` or `RectangularWindow`, or any type with the same members (see [Windows](#windows)).

`RealFFT<256>` is a float transform with the Hann window.

### Windows

| Window | Taper | Coherent gain | Meaning |
|---|---|---|---|
| `HannWindow` | `0.5 - 0.5 cos(2 pi n / Size)` | 0.5 | The default. Low leakage, a main lobe of about two bins. |
| `RectangularWindow` | none | 1 | No taper. A sine at a bin centre fills one bin; a sine between bins leaks into all of them. |

A window is a type with `static constexpr bool tapered`, `static constexpr double coherentGain`, and a `template <uint32 Size, typename Number> static constexpr` member `table` that holds the taper. A window with `tapered == false` never reads its table, so it costs no flash.

### Constants

| Name | Value | Meaning |
|---|---|---|
| `RealFFT::size` | `Size` | Samples per transform |
| `RealFFT::binCount` | `Size / 2 + 1` | Bins returned: DC to the Nyquist frequency |
| `RealFFT::spectrumScale` | `1` for float and double, `2 / Size` for IQ | Factor between `Forward`'s bins and the DFT |
| `RealFFT::amplitudeScale` | `2 / (Size * coherentGain) / spectrumScale` | Factor from a magnitude of `Forward`'s bins to the amplitude of a sine: `4 / Size` for float with Hann |

### Complex

`Complex<Number>` has two members, `re` and `im`, of type `Number`. Both start at zero.

### Global Names

The header declares the global names `Complex`, `HannWindow`, `RectangularWindow` and `FFTDetail`, and it includes `IQ.h`. A program that already defines one of these names must rename its own before it includes the FFT header.

### Forward

`template <typename Source> void Forward(const Source &source, Complex<Number> *spectrum)`

The DFT of `Size` unwindowed samples into `binCount` complex bins. Use it for exact checks; use `Analyze` for spectra of sound.

### Analyze

`template <typename Source> void Analyze(const Source &source, Number *amplitude)`

The windowed amplitude of each bin into `binCount` values. Silence gives zeros. The method reads `Size` samples and writes `binCount` values, and it keeps no reference to either buffer.

### Sources

A source is any type whose `source[i]` gives sample `i` as exactly `Number`. `Analyze` and `Forward` take a source, read each of the `Size` samples once, in index order, and convert nothing else. A pointer or an array of `Number` is a source, and so is any class with the same `operator[]`. The rule is the concept `SampleSourceOf<Source, Number>` in `SampleSource.h`. A source of another type, such as a raw `uint16` buffer for a float transform, is a compile error that names the failed requirement.

| Source | Header | Use |
|---|---|---|
| A pointer or an array of `Number` | (none) | Samples that are already in the number type |
| [`AdcSamples<Number, Bits>`](AdcSamples.md) | `AdcSamples.h` | Raw ADC codes, such as a DMA buffer. Removes the mid code and scales while the FFT loads, so no converted copy exists |
| [`LatestSamples<Number, Size>`](LatestSamples.md) | `LatestSamples.h` | A ring fed from an interrupt. The FFT reads the newest `Size` samples as a sliding window |
| [`FrameCollector<Number, Size, Hop>`](FrameCollector.md) | `FrameCollector.h` | Frames fed from an interrupt, two buffers so the reader holds a stable frame. An optional `Hop` gives overlapping frames |

Each source is its own header, and a program that does not include a header pays nothing for it. Each source is tested in `.tool/sim` against the same samples given as a plain array: the spectra are identical.

```cpp
#include <Utilities/Math/FFT/AdcSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>

static uint16 adcBuffer[1024];
static RealFFT<1024, iq24> fft;
static iq24 amplitude[RealFFT<1024, iq24>::binCount];

AdcSamples<iq24, 12> samples(adcBuffer);
fft.Analyze(samples, amplitude);
```

## Number Types and Scaling

- **float and double** keep the usual scale: `Forward` is the DFT, and a full-scale sine has the DFT magnitude `Size / 2 * A` at its bin.
- **IQ<Q>** halves the outputs of every butterfly stage, with rounding. There are `log2(Size / 2)` stages, so `Forward` returns the DFT times `2 / Size`, which `spectrumScale` states. A full-scale input cannot overflow: with every sample in -1..1, no intermediate value exceeds 1.5, and every bin of `Forward` stays within 2, so `IQ<Q>` for any Q up to 29 holds it.
- The twiddle factors are stored with 30 fraction bits, and each twiddle multiply is a 64-bit product rounded from Q30.
- The magnitude of a bin is the integer square root of its 64-bit squared magnitude, rounded to the nearest raw value. `Analyze` returns the same amplitudes for every number type: a full-scale sine at a bin centre reads as its amplitude in float and in IQ.
- With either window, every amplitude of a full-scale input is at most 2. An IQ holds that only for Q up to 29, which is why `IQ<30>` is rejected.

## Accuracy

Measured against a naive DFT in long double on the same quantized input, with DC of 0.5 and of 1, a sine at a bin centre and one between two bins, uniform noise in -1..1, full-scale noise of +-1, a unit sine and silence, at sizes 256 and 1024, with both windows. The test is `Common/Utilities/Math/FFT/.tool/sim/TestRealFFT.cpp`.

| Type | Forward, worst error divided by Size | Analyze, worst absolute error | Test bound (Analyze) |
|---|---|---|---|
| `float` | 2.8e-8 | 6.0e-8 (one float step for values from 0.5 to 1) | 1.5e-7 |
| `double` | 1.1e-16 | 6.7e-16 | 2e-15 |
| `IQ<16>` | 4.0e-5 | 1.24e-4 (about 8 LSB) | 2.5e-4 |
| `IQ<24>` | 1.4e-7 | 5.6e-7 (about 9 LSB) | 1.2e-6 |

The table generator matches long double to 5e-16 at every size before rounding to `Number`. The IQ twiddles are the nearest Q30 values. Silence gives exact zeros, and no full-scale case saturates any output.

## Sizes

| Type | Size | RAM of an instance | Tables in flash |
|---|---|---|---|
| `float` | 256 | 1024 bytes | 2304 bytes |
| `float` | 1024 | 4096 bytes | 9216 bytes |
| `iq24` | 256 | 1024 bytes | 2304 bytes |
| `iq24` | 1024 | 4096 bytes | 9216 bytes |

The flash tables are the window, the twiddles and the bit-reversal table. The float transform needs no libm sine or cosine. Measured with `arm-none-eabi-g++ -O2`, a float `Analyze` at 256 on Cortex-M4 with the hardware FPU (`-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`) is 118 instructions, with or without `-fno-math-errno`, and calls no library function. With the soft-float ABI (`-mfloat-abi=soft`) the same build calls `sqrtf` for the magnitude. On Cortex-M0+ (no FPU) it calls the soft-float helpers and `sqrtf`.

Sizes of one `Analyze<256>` in a program of its own, built with `-Os` and nano libc, with the sources in the sample program. The baseline is an empty `main`, 396 bytes of text on Cortex-M4.

| Case | Cortex-M4 text | Cortex-M4 RAM (`.data` + `.bss`) | Cortex-M0+ text | Cortex-M0+ RAM |
|---|---|---|---|---|
| `float`, plain array | 932 | 2988 | 5736 | 2988 |
| `float`, `AdcSamples<float, 12>` | 988 | 2476 | 5924 | 2476 |
| `float`, `LatestSamples` | 1008 | 2996 | 5804 | 2996 |
| `float`, `FrameCollector` | 1216 | 4040 | 6024 | 4040 |
| `iq24`, plain array | 1648 | 2596 | 2312 | 2596 |
| `iq24`, `AdcSamples<iq24, 12>` | 1680 | 2084 | 2340 | 2084 |
| `iq24`, `LatestSamples` | 1708 | 2604 | 2380 | 2604 |
| `iq24`, `FrameCollector` | 1904 | 3648 | 2584 | 3648 |

Every RAM count includes the transform's 1024-byte work array and its 516-byte amplitude output. Each row adds the source's own storage: the 1024-byte input array for the plain row, the 512-byte code buffer for `AdcSamples`, the 1024-byte ring for `LatestSamples`, and the two 1024-byte buffers of `FrameCollector`. The float rows carry 344 bytes of `.bss` and 80 bytes of `.data` for the nano libc error state, which the float `sqrtf` of the magnitude pulls in; the `iq24` rows have no library state. The M0+ text includes the soft-float helpers. The M0+ `iq24` rows call `__aeabi_lmul` for the 64-bit products.

Against a converted frame, `AdcSamples` saves one frame of `Size * sizeof(Number)` bytes: 1024 bytes at 256 for float or `iq24`, and 4096 bytes at 1024 for `iq24`. The raw codes stay, because the ADC writes them.

## Choosing the Size

The bin width is `sampleRate / Size`. Two rules apply:

- A longer transform resolves closer frequencies (512 samples at 16 kHz resolve 31.25 Hz; 1024 resolve 15.6 Hz) but covers more time (32 ms against 64 ms at 16 kHz) and costs about twice the time per transform.
- A block of `Size` samples per transform, with no overlap, gives one spectrum every `Size / sampleRate` seconds.
