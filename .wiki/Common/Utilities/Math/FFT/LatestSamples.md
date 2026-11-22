# LatestSamples

A ring of the newest `Size` samples, fed from an interrupt. The FFT reads the ring as a sliding window: each analysis sees the newest samples in time order. The header is `Common/Utilities/Math/FFT/LatestSamples.h`.

## Table of Contents

- [Declaration](#declaration)
- [Window](#window)
- [Example](#example)
- [Interrupt Safety](#interrupt-safety)
- [Cost](#cost)
- [Rules](#rules)

## Declaration

```cpp
template <typename Number, uint32 Size>
class LatestSamples {
public:
    void Push(Number sample);
    Number operator[](uint32 index) const;
};
```

- `Number` is `float`, `double` or `IQ<Q>`, and it must be the transform's `Number`. It must fit one 32-bit word, so `double` is not allowed on a target where a 64-bit store is two bus accesses.
- `Size` is a power of two, at least 2. It is the length of the window, so it is the transform's `Size`.

## Window

`operator[](i)` is sample `i` of the window: 0 is the oldest of the newest `Size` samples, and `Size - 1` is the newest. Before `Size` samples have been pushed, the missing samples read as zero, so the first window is silence followed by the first samples.

With `Size` equal to 4, pushing 1 and 2 gives the window 0, 0, 1, 2. Pushing 3 to 9 in addition gives 6, 7, 8, 9.

## Example

```cpp
#include <Utilities/Math/FFT/LatestSamples.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>

static LatestSamples<iq24, 1024> recent;
static RealFFT<1024, iq24> fft;
static iq24 amplitude[RealFFT<1024, iq24>::binCount];

// In the ADC interrupt, once per sample:
void OnSample(iq24 sample) {
    recent.Push(sample);
}

// In the main loop, whenever a new spectrum is wanted:
void Analyze() {
    fft.Analyze(recent, amplitude);
}
```

## Interrupt Safety

There is one writer, the interrupt that calls `Push`, and one reader, the code that calls `Analyze`.

- `Push` writes the slot of the new sample, then publishes the count with a release store. It does no read-modify-write, so it needs no atomic instruction and works on a Cortex-M0. The slots are accessed with relaxed atomic loads and stores, which compile to plain word accesses on Cortex-M0 and M4.
- The reader loads the count with an acquire load before each sample, so it never reads a slot whose sample is not complete.

A sample is never torn. The window can be mixed, though. The reader takes the count afresh for each sample, so a `Push` during an `Analyze` moves the window for the samples read after it. Each such sample is at most `K` samples newer than its place in the window when the load began, where `K` is the number of pushes during the load. Neighbours can repeat or skip across the seam.

For an exact window, mask the interrupt for the length of the `Analyze` call. The load step is short next to the transform, but it is the whole of the window read.

## Cost

The ring holds `Size * sizeof(Number)` bytes, plus two 32-bit words for the count. Reading it is one acquire load and one array read per sample, which is a few instructions.

## Rules

- `Size` must be a power of two. A static assertion rejects other sizes.
- A single writer. Two interrupts pushing into one ring is not allowed.
- The count is modulo 2^32, which is a whole number of laps for every power-of-two `Size`, so it never needs to be reset.
