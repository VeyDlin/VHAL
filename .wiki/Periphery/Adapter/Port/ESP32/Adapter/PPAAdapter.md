# PPAAdapter

Abstract adapter for ESP32 Pixel Processing Accelerator (PPA). Provides hardware-accelerated fill, blend, and scale/rotate/mirror operations on image buffers using DMA, without loading the CPU.

Header: `#include <Adapter/Port/ESP32/Adapter/PPAAdapter.h>`

## Color

```cpp
struct Color {
    uint8 r = 0;
    uint8 g = 0;
    uint8 b = 0;
    uint8 a = 0xFF;
};
```

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `FillColorFormatOption` | `IOption<uint32>` | Color format for fill operations |
| `BlendColorFormatOption` | `IOption<uint32>` | Color format for blend operations |
| `SrmColorFormatOption` | `IOption<uint32>` | Color format for scale/rotate/mirror |
| `BurstLengthOption` | `IOption<uint32>` | DMA burst length |

## ESP32 Format Constants (`PPAAdapterESP`)

**FillColorFormat:** `ARGB8888`, `RGB888`, `RGB565`, `YUV422`, `GRAY8`

**BlendColorFormat:** `ARGB8888`, `RGB888`, `RGB565`, `A8`, `A4`, `YUV420`, `YUV422`, `GRAY8`

**SrmColorFormat:** `ARGB8888`, `RGB888`, `RGB565`, `YUV420`, `YUV444`, `YUV422`, `GRAY8`

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Rotation` | `Degree0`, `Degree90`, `Degree180`, `Degree270` | Rotation angle |
| `AlphaMode` | `NoChange`, `FixValue`, `Scale`, `Invert` | Alpha channel handling |

## Fill

```cpp
// Simple full-buffer fill
ppa.Fill(buffer, bufferSize, width, height, color, format);

// Advanced fill with offset/region
ppa.Fill(FillConfig{...});
```

### FillConfig

```cpp
struct FillConfig {
    void* buffer = nullptr;
    uint32 bufferSize = 0;
    uint16 picWidth = 0;
    uint16 picHeight = 0;
    uint16 offsetX = 0;
    uint16 offsetY = 0;
    uint16 fillWidth = 0;
    uint16 fillHeight = 0;
    Color color;
    FillColorFormatOption format;
};
```

## Blend

```cpp
ppa.Blend(BlendConfig{...});
```

Blends foreground over background into output buffer with per-layer alpha control.

## Scale/Rotate/Mirror

```cpp
ppa.ScaleRotateMirror(SrmConfig{...});
```

Hardware-accelerated scaling, rotation (0/90/180/270), and mirroring with optional alpha and RGB/byte swap.

## API

| Method | Return | Description |
|--------|--------|-------------|
| `Fill(buffer, size, w, h, color, fmt)` | `ResultStatus` | Fill entire buffer with color |
| `Fill(FillConfig)` | `ResultStatus` | Fill region with color |
| `Blend(BlendConfig)` | `ResultStatus` | Blend two images |
| `ScaleRotateMirror(SrmConfig)` | `ResultStatus` | Transform image |

## Usage Example

```cpp
// Fill framebuffer white
BSP::ppa.Fill(fb, bufferSize, 720, 1280, {0xFF, 0xFF, 0xFF}, APPA::FillColorFormat::RGB888);

// Scale image 2x
BSP::ppa.ScaleRotateMirror({
    .input = { .buffer = src, .picWidth = 360, .picHeight = 640, .blockWidth = 360, .blockHeight = 640 },
    .inFormat = APPA::SrmColorFormat::RGB888,
    .output = { .buffer = dst, .bufferSize = dstSize, .picWidth = 720, .picHeight = 1280 },
    .outFormat = APPA::SrmColorFormat::RGB888,
    .scaleX = 2.0f,
    .scaleY = 2.0f,
});
```
