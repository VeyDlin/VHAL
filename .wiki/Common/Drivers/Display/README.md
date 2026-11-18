# IDisplay

`IDisplay` is the interface between a renderer and a display panel. A renderer, for example an application's bridge to a
graphics library such as LVGL, uses an `IDisplay&` and never a concrete driver. The interface is in
`Common/Drivers/Display/IDisplay.h`; the ST7789 driver implements it.

The interface models a panel with its own frame memory that is written window by window (SPI or parallel controllers). A
framebuffer display, where the MCU scans a frame in its own RAM (LTDC, MIPI DSI video mode), does not fit it. Renderer
bridges are application code: VHAL does not ship one, because each depends on the graphics library and on the kind of
display.

Why it lives in `Common/Drivers/` and not in `Periphery/Adapter/`: the adapters are the interfaces of the MCU's own
peripherals (SPI, GPIO, ...). A display is an external chip on a bus, like the temperature sensors and the battery
chargers, whose interfaces sit next to their drivers. `ITermistor` is in `Common/Drivers/Termistor/` for the same reason.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Pixel Format](#pixel-format)
- [API Reference](#api-reference)
- [Background Writes](#background-writes)
- [DMA-Capable Buffers](#dma-capable-buffers)
- [Implementations](#implementations)

---

## Quick Start

```cpp
#include <Drivers/Display/IDisplay.h>

// Fills a rectangle of the panel with one colour, through the interface only.
void FillTop(IDisplay& display, uint16 height, uint16 color) {
	uint8 rowBytes[2 * 240];
	for (uint32 index = 0; index < 240; index++) {
		rowBytes[index * 2] = static_cast<uint8>(color >> 8);
		rowBytes[index * 2 + 1] = static_cast<uint8>(color & 0xFF);
	}

	display.SetWindow(0, 0, display.GetWidth() - 1, height - 1);
	for (uint16 row = 0; row < height; row++) {
		display.WritePixelBytes(rowBytes, display.GetWidth() * 2);
	}
}
```

The example's row buffer holds 240 pixels, so it assumes a panel no wider than 240 pixels, and it writes one row per call. The
bytes are high byte first, which is the pixel format of the interface (see below).

---

## Pixel Format

```cpp
enum class PixelFormat : uint8 {
    Rgb565BigEndian
};
```

`Rgb565BigEndian` is 16 bits per pixel with the most significant byte first. It is the panel's wire order, so a buffer in
this format goes to the bus without a copy. `GetPixelFormat()` tells the renderer which format the driver expects. For LVGL 9,
`Rgb565BigEndian` is `LV_COLOR_FORMAT_RGB565_SWAPPED`.

---

## API Reference

```cpp
class IDisplay {
public:
    enum class PixelFormat : uint8 { Rgb565BigEndian };

    virtual ~IDisplay() = default;
    virtual uint16 GetWidth() const = 0;
    virtual uint16 GetHeight() const = 0;
    virtual PixelFormat GetPixelFormat() const = 0;
    virtual ResultStatus SetWindow(uint16 x0, uint16 y0, uint16 x1, uint16 y1) = 0;
    virtual ResultStatus WritePixelBytes(const uint8* pixels, uint32 byteCount) = 0;
    virtual ResultStatus WritePixelBytesAsync(const uint8* pixels, uint32 byteCount) = 0;
    virtual bool IsWriteDone() = 0;
    virtual ResultStatus WaitWriteDone(uint32 timeoutMs) = 0;
    virtual ResultStatus CheckDmaBuffer(const uint8* buffer, uint32 size) = 0;
};
```

| Method | Returns | Description |
|--------|---------|-------------|
| `GetWidth()` | `uint16` | Logical width in the current orientation |
| `GetHeight()` | `uint16` | Logical height in the current orientation |
| `GetPixelFormat()` | `PixelFormat` | The byte order and depth the pixel bytes must have |
| `SetWindow(x0, y0, x1, y1)` | `ResultStatus` | Opens the write window, both ends inclusive. The next pixel writes fill it row by row. `outOfRange` when the window does not fit the panel |
| `WritePixelBytes(pixels, byteCount)` | `ResultStatus` | Writes bytes already in the panel's format into the window and waits until the bus has sent them |
| `WritePixelBytesAsync(pixels, byteCount)` | `ResultStatus` | Starts the write in the background and returns. `notSupported` when the driver or the bus cannot do that |
| `IsWriteDone()` | `bool` | `true` when no background write is in flight, also when the driver cannot write in the background |
| `WaitWriteDone(timeoutMs)` | `ResultStatus` | `ok` when the background write has finished or none was started; `timeout` when it has not finished in time |
| `CheckDmaBuffer(buffer, size)` | `ResultStatus` | `ok` when the bus can read the buffer; `invalidParameter` otherwise |

The interface does not include panel setup (reset, orientation, sleep, inversion). Those belong to the driver, and the
application calls them on the concrete driver before it hands the panel to a renderer.

---

## Background Writes

A driver that can write in the background starts the write in `WritePixelBytesAsync` and returns. The rules:

- the buffer must stay unchanged until `IsWriteDone()` is `true`, or `WaitWriteDone()` returns `ok`;
- `WaitWriteDone()` also returns `ok` when no write was started, so waiting before a command is always safe;
- a driver or bus that cannot write in the background returns `notSupported` from `WritePixelBytesAsync`. The caller then
  uses `WritePixelBytes`:

```cpp
ResultStatus status = display.WritePixelBytesAsync(pixels, byteCount);
if (status == ResultStatus::notSupported) {
    status = display.WritePixelBytes(pixels, byteCount);
}
```

A renderer with two draw buffers can start each band in the background this way, render the next band meanwhile, and fall
back to the synchronous write when the panel reports `notSupported`.

---

## DMA-Capable Buffers

`CheckDmaBuffer` asks whether the bus can read a buffer. On the ESP32 the SPI DMA reads internal RAM only, so a buffer in
PSRAM fails. Check every buffer once, at initialisation, before the first write that uses it.

---

## Implementations

| Class | Page | Notes |
|-------|------|-------|
| `ST7789` | [ST7789](../ST7789/README.md) | ST7789V2 panels on an SPI bus. `GetPixelFormat()` is `Rgb565BigEndian` |
