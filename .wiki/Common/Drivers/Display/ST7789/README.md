# ST7789

Driver for the ST7789V2 colour panel over a transmit-only SPI bus, with a data/command line and a reset line. Portable: it uses only the VHAL adapters `ASPI` and `AGPIO`, so the same code runs on any port that provides them.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Wiring and Bus Setup](#wiring-and-bus-setup)
- [Configuration](#configuration)
- [API Reference](#api-reference)
- [Byte Order](#byte-order)
- [Notes](#notes)

---

## Quick Start

```cpp
#include <Drivers/Display/ST7789/ST7789.h>

static ASPI displaySpi = { SPI2_HOST, 15, -1, 13, 5 };
static AGPIO dataCommandPin = { 14, false };
static AGPIO resetPin = { 12, true };

static constexpr ST7789::Config panelConfig = {
	.width = 135,
	.height = 240,
	.offsetX = 52,
	.offsetY = 40,
	.rotation = ST7789::Rotation::Portrait,
	.invertColors = true,
	.bgr = true
};

static ST7789 panel(displaySpi, dataCommandPin, resetPin, panelConfig);

// Configure the bus first (see Wiring and Bus Setup), then:
ResultStatus status = panel.Init();
panel.FillRect(0, 0, panel.GetWidth(), panel.GetHeight(), 0xF800);   // red
```

---

## Wiring and Bus Setup

The driver takes its three hardware handles by reference and never owns them:

| Parameter | Type | Role |
|-----------|------|------|
| `spiBus` | `ASPI&` | SPI master, transmit only |
| `dataCommandPin` | `AGPIO&` | Low for a command byte, high for data bytes |
| `resetPin` | `AGPIO&` | Reset line; `Set()` asserts reset |

`Init()` sets the data/command and reset lines to output itself, keeping the inversion of their `AGPIO` objects. A line left as an input drives nothing, so the panel stays in reset while every SPI write still reports success (this was the cause of a blank screen). The caller therefore only declares the pins with the right polarity, in the BSP, and does not set their mode.

The caller configures the SPI bus before `Init()`: SPI mode 0 (`ClockPolarity::Low`, `ClockPhase::Edge1`), MSB first, `Direction::Tx`, and the highest clock the panel accepts. On the M5StickC PLUS2 that is 40 MHz on SPI2 with MOSI on GPIO15, SCLK on GPIO13 and CS on GPIO5. The driver does not set the bus, because the clock is a board decision.

Polarity lives in the `AGPIO` objects. The M5StickC PLUS2 reset line is active low, so it is declared with inversion set to `true`; `Set()` then drives the line low and holds the panel in reset.

The backlight is not part of this driver. It is a board concern (PWM on the BSP).

---

## Configuration

```cpp
struct Config {
    uint16 width;          // physical width in Portrait, default 135
    uint16 height;         // physical height in Portrait, default 240
    uint16 offsetX;        // column offset of the visible area in Portrait
    uint16 offsetY;        // row offset of the visible area in Portrait
    Rotation rotation;     // Portrait, Landscape, PortraitInverted, LandscapeInverted
    bool invertColors;     // true sends INVON (0x21)
    bool bgr;              // true selects BGR subpixel order (MADCTL bit 3)
};
```

The M5StickC PLUS2 panel needs offsets 52 and 40 in Portrait, `invertColors = true` and `bgr = true`. These values reproduce the working self-test of the main project.

### Offsets per Rotation

The controller RAM is 240 columns by 320 rows. The visible panel sits inside it at the configured Portrait offsets. The driver derives the offsets of the other orientations from the MADCTL mirror bits:

- MX mirrors the RAM columns, so the visible area moves to the far end of the RAM. Its column offset becomes `240 - width - offsetX`.
- MY mirrors the RAM rows the same way. Its row offset becomes `320 - height - offsetY`.
- MV (the landscape orientations) exchanges the window's axes. The column address then runs along the RAM rows, and the row address along the RAM columns.

The derivation assumes that the window commands (CASET, RASET) take logical coordinates, after MADCTL is applied. That is the usual ST7789 behaviour, and it is how the driver has always sent them. Under that assumption the landscape offsets follow the exchange: the column offset comes from the row axis, and the row offset from the column axis. This assumption is what the landscape check on the panel tests.

For the M5StickC PLUS2 (width 135, height 240, offsets 52 and 40):

| Rotation | MADCTL | MX | MY | MV | Column offset | Row offset |
|----------|--------|----|----|----|---------------|------------|
| Portrait | 0x00 | no | no | no | 52 | 40 |
| Landscape | 0x60 | yes | no | yes | 40 | 53 |
| PortraitInverted | 0xC0 | yes | yes | no | 53 | 40 |
| LandscapeInverted | 0xA0 | no | yes | yes | 40 | 52 |

The row gap of 80 (320 - 240) is split evenly, 40 and 40, so MY does not change its offset. The column gap of 105 (240 - 135) is odd, so MX moves the offset from 52 to 53. Landscape is therefore 40 columns and 53 rows, and LandscapeInverted is 40 and 52.

These values are derived from the address mapping. On the M5StickC PLUS2, Portrait and Landscape were checked by eye with a 1 px frame at the visible edges (no missing edge, no garbage strip); PortraitInverted and LandscapeInverted have not been checked.

---

## API Reference

| Method | Returns | Description |
|--------|---------|-------------|
| `Init()` | `ResultStatus` | Sets the data/command and reset lines to output, reset, sleep out, RGB565, orientation, inversion, normal mode, display on, then clear to black |
| `SetRotation(Rotation)` | `ResultStatus` | Writes MADCTL and swaps the logical width and height for the landscape orientations |
| `SetInversion(bool)` | `ResultStatus` | INVON or INVOFF |
| `SetSleep(bool)` | `ResultStatus` | Sleep in, or sleep out followed by a 120 ms wait |
| `GetWidth()` / `GetHeight()` | `uint16` | Logical size in the current orientation |
| `SetWindow(x0, y0, x1, y1)` | `ResultStatus` | Sets the write window with both ends inclusive, then starts a memory write. `outOfRange` when the window is empty or leaves the panel |
| `WritePixels(rgb565, count)` | `ResultStatus` | Streams `count` native RGB565 pixels into the window opened by the last `SetWindow`, swapping each one through a staging buffer |
| `WritePixelBytes(bytes, byteCount)` | `ResultStatus` | Streams bytes already in panel order (high byte first) into the window, with no copy. The bus splits the buffer into transfers |
| `WritePixelBytesAsync(bytes, byteCount)` | `ResultStatus` | Starts the same write on the bus in the background and returns. `notSupported` when the bus cannot do it, so the caller falls back to `WritePixelBytes`. The buffer must stay unchanged until `IsWriteDone()` is true |
| `IsWriteDone()` | `bool` | True when no background write is in flight (also when the bus cannot do one) |
| `WaitWriteDone(timeoutMs)` | `ResultStatus` | Waits up to `timeoutMs` for the background write. `ok` when it has finished or none was started, `timeout` otherwise |
| `CheckDmaBuffer(bytes, byteCount)` | `ResultStatus` | `ok` when the bus can read the buffer (on the ESP32, internal RAM only). `invalidParameter` otherwise |
| `FillRect(x, y, w, h, color)` | `ResultStatus` | Clips to the panel, sets the window and fills it with one colour |

No method allocates memory. Each transfer stages pixels in a 512-byte member buffer (256 pixels), which is DMA-friendly because it lives in internal RAM.

Each call that touches the bus returns the `ResultStatus` of the transfer. `Init()` stops at the first failing command.

---

## Byte Order

The panel expects the most significant byte of each pixel first. Two paths exist:

- `WritePixelBytes` takes bytes already in that order. The LVGL bridge uses it with `LV_COLOR_FORMAT_RGB565_SWAPPED`, so the swap is done by the renderer and the buffer goes to the bus unchanged.
- `WritePixels` takes native `uint16` values and swaps them through a 512-byte staging buffer, one copy per 256 pixels. Use it only when the caller has no swapped buffer.

Do not combine the two for the same data: a swapped buffer passed to `WritePixels` is swapped twice.

---

## Background Writes

`WritePixelBytesAsync` lets the panel write run while the CPU renders the next frame. The rules the driver keeps:

- Every command and data write waits for a background write to finish before it changes the data/command line. A background pixel write reads its data while that line is high, so changing the line early would turn the rest of the write into command bytes. This wait is internal, so a caller that forgets to wait cannot corrupt the screen; it only waits longer.
- The window commands of a flush (CASET, RASET, RAMWR) are synchronous. They reach the panel before the pixel data of the same flush, because the data is only started after them.
- The bus decides how the write is split. The chip select stays low across the split, so the panel sees one memory write (see `SPIAdapter.md`, ESP32 port).
- A bus without background writes returns `notSupported` from `WritePixelBytesAsync` and `WaitWriteBytes`. The driver treats that as "nothing in flight", so the synchronous calls work on every port.

The buffer used by `WritePixelBytesAsync` must be in memory the bus can read. Check it once with `CheckDmaBuffer` at initialisation, as `LvglDisplay::Init` does.

---

## Notes

- Offsets are given for Portrait and derived for the other orientations (see Offsets per Rotation). Portrait and Landscape are checked on the M5StickC PLUS2; the two inverted orientations are derived, not checked.
- Behaviour change: PortraitInverted now uses column offset 53 (it was 52), and Landscape uses row offset 53 (it was 52). Code that relied on the old values must be checked on the panel.
- `Init()` leaves the panel cleared to black.
- The driver has no knowledge of LVGL. The bridge is in `Utilities/UI/LVGL/LvglDisplay.h`.
