# DSIAdapter

Abstract adapter for MIPI DSI (Display Serial Interface) peripherals on ESP32. Supports video and command modes for driving LCD panels via DSI lanes with configurable timing, color format, frame buffers, and DCS command interface.

Header: `#include <Adapter/Port/ESP32/Adapter/DSIAdapter.h>`

> **Note:** Previously a cross-platform adapter, now ESP32-specific since MIPI DSI is only available on ESP32-P4.

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `ClockSourceOption` | `IOption<uint32>` | DSI PLL clock source |
| `ColorFormatOption` | `IOption<uint32>` | Pixel color format |
| `DataEndianOption` | `IOption<uint32>` | Data byte endianness |
| `ColorSpaceOption` | `IOption<uint32>` | Color space (RGB/YUV) |
| `ColorRangeOption` | `IOption<uint32>` | Color range (Limited/Full) |
| `YuvSampleOption` | `IOption<uint32>` | YUV subsampling mode |
| `YuvConvStdOption` | `IOption<uint32>` | YUV conversion standard |
| `YuvPackOrderOption` | `IOption<uint32>` | YUV packing order |
| `FrameBufferCountOption` | `IOption<uint8>` | Number of frame buffers |

## Parameters

```cpp
struct Parameters {
    Mode mode = Mode::Video;
    uint8 laneCount = 2;
    uint32 laneBitRateMbps = 500;
    uint8 virtualChannel = 0;
    ColorFormatOption colorFormat;
    VideoTiming timing;
    uint32 pixelClockKHz = 0;
    ClockSourceOption clockSource;
    Polarity hsPolarity = Polarity::Low;
    Polarity vsPolarity = Polarity::Low;
    Polarity dePolarity = Polarity::Low;
    FrameBufferCountOption frameBufferCount;
};
```

## Video Timing

```cpp
struct VideoTiming {
    uint16 hsyncWidth = 0;
    uint16 hBackPorch = 0;
    uint16 hFrontPorch = 0;
    uint16 activeWidth = 0;
    uint16 vsyncWidth = 0;
    uint16 vBackPorch = 0;
    uint16 vFrontPorch = 0;
    uint16 activeHeight = 0;
};
```

## Enums

| Enum | Values | Description |
|------|--------|-------------|
| `Mode` | `Video`, `Command` | DSI operating mode |
| `Polarity` | `Low`, `High` | Signal polarity |
| `Irq` | `TransferDone`, `RefreshDone` | Interrupt sources |
| `Error` | `None`, `Transmission`, `Protocol`, `Overflow` | Error types |

## ESP32 Config (`DSIAdapterESP`)

```cpp
struct Config {
    bool useDma2d = false;
    DataEndianOption dataEndian = DataEndian::Big;
    ColorSpaceOption colorSpace = ColorSpace::RGB;
    ColorRangeOption colorRange = ColorRange::Full;
};
```

Access via `display.config.useDma2d = true;` before calling `Start()`.

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply DSI configuration |
| `WriteCommand(uint8 cmd, ...)` | `ResultStatus` | Send DCS command (multiple overloads) |
| `ReadCommand<T>(uint8 cmd)` | `Result<T>` | Read DCS response |
| `Start()` | `ResultStatus` | Start DSI video output (creates DPI panel) |
| `Stop()` | `ResultStatus` | Stop DSI output |
| `GetFrameBuffer(uint8 index)` | `void*` | Get framebuffer pointer |
| `DrawBitmap(x0, y0, x1, y1, data)` | `ResultStatus` | Draw bitmap to panel |
| `Mirror(bool x, bool y)` | `ResultStatus` | Mirror display |
| `SwapXY(bool swap)` | `ResultStatus` | Swap X/Y axes |
| `InvertColor(bool invert)` | `ResultStatus` | Invert colors |
| `DisplayOnOff(bool on)` | `ResultStatus` | Turn display on/off |
| `DisplaySleep(bool sleep)` | `ResultStatus` | Enter/exit sleep |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq)>` | Refresh/transfer event |
| `onError` | `std::function<void(Error)>` | Protocol/transmission error |

## Usage Example

```cpp
BSP::display.SetParameters({
    .mode = ADSI::Mode::Video,
    .laneCount = 2,
    .laneBitRateMbps = 1000,
    .colorFormat = ADSI::ColorFormat::RGB888,
    .timing = {
        .hsyncWidth = 50, .hBackPorch = 239, .hFrontPorch = 33,
        .activeWidth = 720,
        .vsyncWidth = 30, .vBackPorch = 20, .vFrontPorch = 2,
        .activeHeight = 1280,
    },
    .pixelClockKHz = 80000,
    .clockSource = ADSI::ClockSource::Default,
    .frameBufferCount = ADSI::FrameBufferCount::Double,
});

BSP::display.config.useDma2d = false;
BSP::display.Start();

void* fb = BSP::display.GetFrameBuffer(0);
BSP::display.DrawBitmap(0, 0, 720, 1280, fb);
```
