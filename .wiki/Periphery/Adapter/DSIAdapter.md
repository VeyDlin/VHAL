# DSIAdapter

Abstract adapter for MIPI DSI (Display Serial Interface) peripherals. Supports video and command modes for driving LCD panels via DSI lanes with configurable timing, color format, and DCS command interface.

Header: `#include <Adapter/DSIAdapter.h>`

## Template Parameter

```cpp
template<typename HandleType>
class DSIAdapter : public IAdapter;
```

## Option Types

| Type | Base | Description |
|------|------|-------------|
| `ClockSourceOption` | `IOption<uint32>` | DSI PLL clock source |
| `ColorFormatOption` | `IOption<uint32>` | Pixel color format |

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
    uint8 frameBufferCount = 1;
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
| `Irq` | `RefreshDone`, `TearingEffect`, `TransferDone` | Interrupt sources |
| `Error` | `None`, `Transmission`, `Protocol`, `Overflow` | Error types |

## API

| Method | Return | Description |
|--------|--------|-------------|
| `SetParameters(Parameters val)` | `ResultStatus` | Apply DSI configuration |
| `WriteCommand(uint8 cmd, const uint8* params, uint32 size)` | `ResultStatus` | Send DCS command |
| `ReadCommand(uint8 cmd, uint8* data, uint32 size)` | `ResultStatus` | Read DCS response |
| `Start()` | `ResultStatus` | Start DSI output |
| `Stop()` | `ResultStatus` | Stop DSI output |
| `GetFrameBuffer(uint8 index)` | `void*` | Get framebuffer pointer |
| `IrqHandler()` | `void` | Call from DSI IRQ |

## Callbacks

| Callback | Type | Description |
|----------|------|-------------|
| `onInterrupt` | `std::function<void(Irq)>` | Refresh/transfer event |
| `onError` | `std::function<void(Error)>` | Protocol/transmission error |

## BSP Configuration Example

```cpp
// BSP.h
class BSP {
public:
    static ADSI dsi;
};

// BSP.cpp
ADSI BSP::dsi = { DSI };

void BSP::InitAdapterPeripheryEvents() {
    dsi.beforePeripheryInit = []() {
        // Enable DSI and LTDC clocks
        __HAL_RCC_DSI_CLK_ENABLE();
        __HAL_RCC_LTDC_CLK_ENABLE();

        NVIC_SetPriority(DSI_IRQn, 3);
        NVIC_EnableIRQ(DSI_IRQn);

        return ResultStatus::ok;
    };
}
```

## Usage Example

```cpp
BSP::dsi.SetParameters({
    .mode = ADSI::Mode::Video,
    .laneCount = 2,
    .laneBitRateMbps = 500,
    .colorFormat = ADSI::ColorFormat::RGB888,
    .timing = {
        .hsyncWidth = 2, .hBackPorch = 1, .hFrontPorch = 1,
        .activeWidth = 480,
        .vsyncWidth = 2, .vBackPorch = 1, .vFrontPorch = 1,
        .activeHeight = 800
    },
    .frameBufferCount = 2
});

// Send display initialization commands
BSP::dsi.WriteCommand(0x11, nullptr, 0); // Sleep Out
System::DelayMs(120);
BSP::dsi.WriteCommand(0x29, nullptr, 0); // Display On

BSP::dsi.Start();

// Draw to framebuffer
auto* fb = static_cast<uint32*>(BSP::dsi.GetFrameBuffer(0));
```
