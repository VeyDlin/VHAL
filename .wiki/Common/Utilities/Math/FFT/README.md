# FFT

A radix-2 Fast Fourier Transform of a real signal, for spectrum analysis on a microcontroller. The class is portable: it uses only `<cmath>` and the VHAL data types, holds all its buffers, and allocates nothing.

## Table of Contents

- [Quick Start](#quick-start)
- [How It Works](#how-it-works)
- [API Reference](#api-reference)
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

Keep the object static or in a long-lived structure: an instance holds about 8.7 KB for `Size` 512 in its own members (the window, the twiddles, the bit-reversal table, the packed work array and the output bins).

## How It Works

- The constructor computes the Hann window, the twiddle factors (the cosines and sines of the full circle, one table for every stage) and the bit-reversal permutation. Nothing is computed per transform except the butterflies and the unpacking.
- A real signal of `Size` samples is packed into a complex signal of `Size / 2` points (even samples as the real part, odd samples as the imaginary part). The complex FFT of that signal is unpacked into the `Size / 2 + 1` bins of the real spectrum.
- `Analyze` applies the periodic Hann window (`0.5 - 0.5 cos(2 pi n / Size)`) and scales the magnitudes by `4 / Size`, so a sine of amplitude A at a bin centre reads as A. A sine between two bins reads as the Hann main lobe, which is about 0.85 of its amplitude at the nearest bin.
- `Forward` is the plain DFT of the signal without the window and without scaling. Its bins are the DFT values, so Parseval's relation holds for them: the sum of the squared samples equals `(|X0|^2 + |X[Size/2]|^2 + 2 * sum |Xk|^2) / Size`.

## API Reference

### Template Parameter

`template <uint32 Size>`: the number of samples, a power of two, at least 4. A static assertion rejects other values.

### Constants

| Name | Value | Meaning |
|---|---|---|
| `RealFFT<Size>::size` | `Size` | Samples per transform |
| `RealFFT<Size>::binCount` | `Size / 2 + 1` | Bins returned: DC to the Nyquist frequency |
| `RealFFT<Size>::amplitudeScale` | `4 / Size` | Factor from the windowed magnitude to the amplitude |

### Complex

`RealFFT<Size>::Complex` has two floats, `re` and `im`.

### Constructor

`RealFFT()` computes the window, the twiddles and the bit-reversal table. Create one instance and reuse it.

### Forward

`void Forward(const float *input, Complex *spectrum)`

The DFT of `Size` unwindowed samples into `binCount` complex bins. Use it for exact checks; use `Analyze` for spectra of sound.

### Analyze

`void Analyze(const float *samples, float *amplitude)`

The Hann-windowed amplitude of each bin into `binCount` floats. Silence gives zeros. The method reads `Size` samples and writes `binCount` values, and it keeps no reference to either buffer.

## Choosing the Size

The bin width is `sampleRate / Size`. Two rules apply:

- A longer transform resolves closer frequencies (512 samples at 16 kHz resolve 31.25 Hz; 1024 resolve 15.6 Hz) but covers more time (32 ms against 64 ms at 16 kHz) and costs about twice the time per transform.
- A block of `Size` samples per transform, with no overlap, gives one spectrum every `Size / sampleRate` seconds.
