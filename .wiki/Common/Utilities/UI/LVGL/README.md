# LVGL Display Bridge

`LvglDisplay` connects LVGL 9 to any display behind the [`IDisplay`](../../../Drivers/Display/README.md) interface, in partial render mode. The application allocates the draw buffers and passes them in. The bridge creates the LVGL display, flushes each filled area through the panel, and feeds LVGL's tick from `System::GetMs()`. With two buffers the flush runs in the background, so LVGL renders the next band while the last one is written.

The bridge is compiled only when the project's `VHALConfig.h` defines `VHAL_LVGL`. LVGL itself is an external dependency: the project adds it and defines the macro, and VHAL does not vendor it. This header includes only LVGL's public `lvgl.h`, and it includes no ESP-IDF header.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Enabling the Bridge](#enabling-the-bridge)
- [Constructor and Buffers](#constructor-and-buffers)
- [Choosing the configuration](#choosing-the-configuration)
- [API Reference](#api-reference)
- [Flush Path](#flush-path)
- [DMA-Capable Buffers](#dma-capable-buffers)
- [Requirements](#requirements)

---

## Quick Start

```cpp
#include <VHAL.h>
#include <Drivers/Display/ST7789/ST7789.h>
#include <Utilities/UI/LVGL/LvglDisplay.h>

static ST7789 panel(displaySpi, dataCommandPin, resetPin, panelConfig);

// Two bands of 40 lines for a 135 pixel wide panel. The application owns them, so it sizes them.
static constexpr uint16 bandLines = 40;
static constexpr uint32 bandBytes = LvglDisplay::BufferBytes(135, bandLines);
alignas(4) static uint8 drawBand[bandBytes];
alignas(4) static uint8 secondDrawBand[bandBytes];

static LvglDisplay lvglDisplay(panel, bandLines, drawBand, secondDrawBand);

void Setup() {
    panel.Init();
    lvglDisplay.Init();

    lv_obj_t* label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello");
}

void Loop() {
    uint32 nextMs = lvglDisplay.TimerHandler();
    System::DelayMs(5);
}
```

With one buffer, pass only `drawBand`; the flush is then synchronous.

---

## Enabling the Bridge

Define `VHAL_LVGL` in the project's `VHALConfig.h`. Without it the header compiles to nothing.

```cpp
#define VHAL_LVGL
```

On ESP-IDF, add the managed component and pin it exactly. The M5AI display test uses `main/idf_component.yml`:

```yaml
dependencies:
  lvgl/lvgl: "9.6.0"
```

Configure LVGL through `sdkconfig.defaults`. The options the bridge needs are:

- `CONFIG_LV_COLOR_DEPTH=16`
- `CONFIG_LV_DRAW_SW_SUPPORT_RGB565_SWAPPED=y` (the default). The bridge draws `LV_COLOR_FORMAT_RGB565_SWAPPED`, and the renderer needs this format compiled in. Without it the panel shows wrong colours and no error is reported
- `CONFIG_LV_MEM_SIZE` for LVGL's object heap (the display test uses 32768 bytes)

Any other LVGL port works too. The bridge needs `lv_init`, `lv_display_*` (including `lv_display_set_flush_wait_cb`), `lv_tick_set_cb`, `lv_timer_handler`, and `lv_refr_now`, all from LVGL 9.

---

## Constructor and Buffers

```cpp
LvglDisplay(IDisplay& panel, uint16 lineCount, std::span<uint8> buffer, std::span<uint8> secondBuffer = {});

static constexpr uint32 BufferBytes(uint16 width, uint16 lineCount);
```

| Parameter | Meaning |
|-----------|---------|
| `panel` | The display, through `IDisplay`. It must outlive the bridge |
| `lineCount` | Lines in each band. Must not be 0 |
| `buffer` | The first draw buffer. The bridge uses `BufferBytes(width, lineCount)` bytes of it, so it must be at least that large |
| `secondBuffer` | An optional second draw buffer of the same size. Empty selects the synchronous flush with one buffer; given, it selects the background flush with two |

`BufferBytes(width, lineCount)` is `width * lineCount * 2`, RGB565 at two bytes a pixel. The width is the panel's width in its current orientation. For a 135 pixel wide panel, one 20-line band is 5400 bytes and one 80-line band is 21600 bytes. Two buffers double that.

The application allocates the buffers, so the bridge has no static storage and no width limit. Allocate them where the bus can read them (see [DMA-Capable Buffers](#dma-capable-buffers)). `Init()` checks the sizes, the DMA capability and the panel's pixel format, and returns `invalidParameter` for a buffer that is too small.

### Choosing the configuration

Measured on the M5StickC PLUS2 at `-O2`, with PSRAM enabled (`CONFIG_SPIRAM=y`), while scrolling the display test's paragraph (`tools/display-test/README.md`). Each figure is the mean of nine two-second windows after the scroll starts:

| Configuration | Draw buffers | Full redraw (ms) | Scroll fps | Render per frame (ms) | Flush not overlapped (ms) | Worst frame (ms) |
|---------------|--------------|------------------|------------|-----------------------|---------------------------|------------------|
| Synchronous, 1 x 80 lines | 21.6 KB | 44.4 | 21.1 | 27.2 | 12.8 | 44.7 |
| Asynchronous, 2 x 40 lines | 21.6 KB | 47.8 | 20.9 | 37.5 | 2.9 | 47.7 |
| **Asynchronous, 2 x 80 lines** | 43.2 KB | **34.9** | **25.8** | 25.7 | 1.9 | 33.5 |

For comparison, the same synchronous 80-line configuration with PSRAM disabled measured 23.2 fps and 23.3 ms of render per frame. Enabling PSRAM made the render about 17 % slower (27.2 ms against 23.3 ms). The cause is not isolated here; the figures are the only evidence.

"Render per frame" is the time of a frame outside the flush and wait callbacks. "Flush not overlapped" is the time LVGL spends in the flush call and in its waits for an earlier write. A write that runs while LVGL renders does not count there, which is why the asynchronous rows show less flush time.

The earlier LineCount sweep (synchronous, one buffer, PSRAM disabled, `-Os` unless noted) showed the same trend: the render cost falls as the band grows, because LVGL walks the label again for each band. Frames per second: 20 lines about 10.7, 40 lines about 16, 80 lines about 19 (about 23 at `-O2`), full frame about 24.

Every configuration writes the same pixels. The display test checks this with a checksum of the bytes sent to the panel, which is the same in all rows.

---

## API Reference

| Method | Returns | Description |
|--------|---------|-------------|
| `Init()` | `ResultStatus` | Checks the buffer sizes, that each buffer is DMA-capable (`IDisplay::CheckDmaBuffer`) and that the panel's pixel format is `Rgb565BigEndian` (`notSupported` otherwise). Then calls `lv_init()`, sets the tick, creates the display, and registers the buffers and the flush and flush-wait callbacks. Call once, after the panel is initialised |
| `BufferBytes(width, lineCount)` | `uint32` (static, constexpr) | The size of one draw buffer, for sizing the arrays |
| `TimerHandler()` | `uint32` | Runs LVGL timers and redraws. Returns the milliseconds until the next call is due |
| `WaitIdle()` | `void` | Waits until the last background write has finished and marks it ready. Call before reading the panel or reusing the buffers outside LVGL |
| `GetDisplay()` | `lv_display_t*` | The LVGL display, for `lv_refr_now` and similar calls |
| `GetLastFlushStatus()` | `ResultStatus` | Result of the last bus transfer made for LVGL, or of the last wait that failed |
| `Flush(display, area, pixels)` | `void` (static, public) | The LVGL flush callback. Public so a test can wrap it. Do not call it directly |
| `WaitFlush(display)` | `void` (static, public) | The LVGL flush-wait callback. Public for the same reason |

---

## Flush Path

LVGL renders into the band buffer in `LV_COLOR_FORMAT_RGB565_SWAPPED`, which is the panel's byte order.

With one buffer, the flush callback calls `SetWindow`, then `WritePixelBytes` with the area's byte count (two per pixel), then `lv_display_flush_ready`. The transfer is synchronous.

With two buffers, the flush callback calls `SetWindow` and then `WritePixelBytesAsync`. It does not signal ready. LVGL renders the next band into the other buffer. Before LVGL reuses a buffer that is still being written, it calls the flush-wait callback, which waits for the write and returns; LVGL then clears its busy state itself. If the panel cannot write in the background (`notSupported`), the flush falls back to `WritePixelBytes` and signals ready at once.

The flush-wait callback is the only place where the bridge blocks. It waits up to `flushTimeoutMs` (1000 ms). On timeout the error is recorded and the buffer is handed over anyway, so the UI keeps running, but the affected band may be wrong.

The panel must not see the data/command line change while a background write is running. `ST7789` waits for the write before any command or data, and `SetWindow` is synchronous, so the window commands (CASET, RASET, RAMWR) reach the panel before the pixel data of the same flush. The next flush cannot start until the callbacks have collected the previous write.

The callback reaches the panel through the bridge pointer stored with `lv_display_set_user_data`. A failed transfer is recorded in `GetLastFlushStatus()`.

The tick is `System::GetMs()` truncated to 32 bits, in milliseconds. The system tick therefore needs to run at 1 kHz for LVGL's timing to be correct.

---

## DMA-Capable Buffers

Every draw buffer must lie in memory the port's bus can read. `Init()` checks this through `IDisplay::CheckDmaBuffer`, which the panel answers from its bus. For the ST7789 on the ESP32 port, the SPI port uses `esp_ptr_dma_capable()` and returns `invalidParameter` for a buffer outside internal RAM. `Init()` then fails instead of corrupting the screen.

The rule, as the ESP-IDF memory guide states it (`docs/en/api-reference/system/mem_alloc.rst`): "Use the `MALLOC_CAP_DMA` flag to allocate memory which is suitable for use with hardware DMA engines (for example SPI and I2S). This capability flag excludes any external PSRAM."

On the classic ESP32, the SPI DMA cannot read PSRAM. Static arrays in the application are placed in internal DRAM, which is why the examples use them. The check is there so a later change that moves a buffer into PSRAM fails at `Init()`.

---

## Requirements

- LVGL 9. The bridge uses `lv_display_set_buffers` with `LV_DISPLAY_RENDER_MODE_PARTIAL`, `lv_display_set_flush_wait_cb` and `lv_display_flush_ready`, which are LVGL 9 names.
- `VHAL_LVGL` defined in the project's `VHALConfig.h`, and LVGL added to the project (see [Enabling the Bridge](#enabling-the-bridge)).
- An `IDisplay` that outlives the bridge. The examples use an `ST7789`.
- Draw buffers that outlive the bridge, at least `BufferBytes(width, lineCount)` bytes each, in DMA-capable memory (see above).
- `System::Init()` and a running system tick before `Init()`.
