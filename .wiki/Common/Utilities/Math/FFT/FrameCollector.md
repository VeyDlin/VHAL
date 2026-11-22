# FrameCollector

Collects frames of `Size` samples from an interrupt, and gives the newest complete frame to the reader as a sample source. Two buffers take turns, so the reader analyses a stable frame while the next one fills. A `Hop` smaller than `Size` makes overlapping frames. The header is `Common/Utilities/Math/FFT/FrameCollector.h`.

## Table of Contents

- [Declaration](#declaration)
- [Frames](#frames)
- [Example](#example)
- [Leases and Dropped Frames](#leases-and-dropped-frames)
- [Overlap](#overlap)
- [Interrupt Safety](#interrupt-safety)
- [Cost](#cost)

## Declaration

```cpp
template <typename Number, uint32 Size, uint32 Hop = Size>
class FrameCollector {
public:
    void Push(Number sample);
    Frame Latest();

    class Frame {
    public:
        bool Valid() const;
        Number operator[](uint32 index) const;
    };
};
```

- `Number` is `float`, `double` or `IQ<Q>`, one 32-bit word at most.
- `Size` is the frame length, and it is the transform's `Size`.
- `Hop` is the number of new samples per frame, from 1 to `Size`. It defaults to `Size`, which gives frames that do not overlap.

## Frames

`Push` adds one sample. Each `Size` samples form a frame, and the frame is published as the newest complete frame.

`Latest` returns a `Frame` that holds the newest published frame. Its `operator[]` reads the frame, so a `Frame` is a sample source for `Analyze`. `Valid` is false when no frame has been published yet, when the writer is busy with the buffer, or when a frame is already held; an invalid frame reads as zeros.

A `Frame` cannot be copied or moved. It holds a lease on its buffer, and the lease ends when the `Frame` is destroyed. A `Latest` call while a frame is held returns an invalid frame, so at most one frame is valid at a time.

## Example

```cpp
#include <Utilities/Math/FFT/FrameCollector.h>
#include <Utilities/Math/FFT/RealFFT.h>
#include <Utilities/Math/IQMath/IQMath.h>

static FrameCollector<iq24, 1024> frames;
static RealFFT<1024, iq24> fft;
static iq24 amplitude[RealFFT<1024, iq24>::binCount];

// In the ADC interrupt, once per sample:
void OnSample(iq24 sample) {
    frames.Push(sample);
}

// In the main loop:
void Analyze() {
    FrameCollector<iq24, 1024>::Frame frame = frames.Latest();
    if (frame.Valid()) {
        fft.Analyze(frame, amplitude);
    }
}
```

The `Frame` is destroyed at the end of `Analyze`, which releases its buffer.

## Leases and Dropped Frames

- The reader holds at most one frame at a time. Calling `Latest` while a frame is held returns an invalid frame and leaves the lease alone, so the held buffer is never written.
- The writer never writes a leased buffer. When the next frame needs the buffer the reader holds, the writer drops that frame: the samples of it are not stored and no frame is published. The frame after a dropped one starts without overlap, so there is a gap of one full frame before the frames are regular again.
- The reader's frame is always whole and stable. Its samples do not change while it is held, and it is never mixed with samples of another frame.

A published frame's buffer is reused when the writer has filled the other buffer, which takes one frame time after the frame was published. A reader that still holds the newest frame at that point loses the next frame to a drop. With `Size` 1024 at 16 kHz, a frame time is 64 ms, so the reader should get its frame and finish the `Analyze` within that time.

## Overlap

With `Hop` smaller than `Size`, each new frame keeps the last `Size - Hop` samples of the previous frame and adds `Hop` new ones. A frame is published every `Hop` samples.

The overlap is kept by copying: when a frame is published, the writer copies the last `Size - Hop` samples into the buffer it fills next, inside `Push`. Each frame copies `Size - Hop` samples, so the copy cost per sample is `(Size - Hop) / Hop`. With `Hop` equal to `Size / 2`, that is one copy per sample, which is a few instructions per sample in the interrupt. With `Hop` equal to `Size`, nothing is copied.

Overlap does not change the buffers: there are still two.

## Interrupt Safety

There is one writer, the interrupt that calls `Push`, and one reader, the code that calls `Latest` and `Analyze`.

Each shared word has one writer. The writer announces the buffer it fills before it checks the reader's lease, and the reader announces its lease before it checks the writer's announcement. Each side has a sequentially consistent fence between the two steps, so whichever comes second sees the other. Neither side needs a read-modify-write.

The writer does not block, and `Push` takes a fixed number of steps per sample, plus the copy at each published frame.

## Cost

- RAM: `2 * Size * sizeof(Number)` for the buffers, plus a few words of state. A float frame of 256 is 2048 bytes; an `iq24` frame of 1024 is 8192 bytes.
- Flash: the code of `Push`, `Latest` and the frame. No library is linked for the collector itself.
- The usual alternative, one buffer that the interrupt fills and the reader reads without a lease, is not safe: the next frame overwrites the one being read.
