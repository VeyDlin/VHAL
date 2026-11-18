# LVGL Display Bridge

`LvglDisplay` connects LVGL 9 to an `ST7789` panel in partial render mode. It owns the draw buffers, creates the LVGL display, flushes each filled area through the panel, and feeds LVGL's tick from `System::GetMs()`. With two buffers the flush runs in the background, so LVGL renders the next band while the last one is written.

LVGL itself is an external dependency. The project adds it; VHAL does not vendor it. This header includes only LVGL's public `lvgl.h`, and it includes no ESP-IDF header.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Adding LVGL to a Project](#adding-lvgl-to-a-project)
- [Template Parameters](#template-parameters)
- [API Reference](#api-reference)
- [Flush Path](#flush-path)
- [DMA-Capable Buffers](#dma-capable-buffers)
- [Requirements](#requirements)

---

## Quick Start

```cpp
#include <Drivers/Display/ST7789/ST7789.h>
#include <Utilities/UI/LVGL/LvglDisplay.h>

static ST7789 panel(displaySpi, dataCommandPin, resetPin, panelConfig);
static LvglDisplay<40, 135, 2> lvglDisplay(panel);

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

---

## Adding LVGL to a Project

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

## Template Parameters

```cpp
template<uint16 LineCount = 20, uint16 MaxWidth = 135, uint8 BufferCount = 1>
class LvglDisplay;
```

| Parameter | Default | Meaning |
|-----------|---------|---------|
| `LineCount` | 20 | Number of lines in each draw buffer |
| `MaxWidth` | 135 | Widest panel the buffers support. `Init()` fails with `outOfRange` when the panel is wider |
| `BufferCount` | 1 | `1` flushes synchronously. `2` flushes in the background and renders the next band while the last one is written. Other values do not compile |

Each buffer is static storage of `MaxWidth * LineCount * 2` bytes: 5400 bytes for one 20-line buffer, 21600 bytes for one 80-line buffer. Two buffers double that. The buffers are shared by every `LvglDisplay` of the same instantiation.

### Choosing the configuration

Measured on the M5StickC PLUS2 at `-O2`, with PSRAM enabled (`CONFIG_SPIRAM=y`), while scrolling the display test's paragraph (`tools/display-test/README.md`). Each figure is the mean of nine two-second windows after the scroll starts:

| Configuration | Static DRAM | Full redraw (ms) | Scroll fps | Render per frame (ms) | Flush not overlapped (ms) | Worst frame (ms) |
|---------------|-------------|------------------|------------|-----------------------|---------------------------|------------------|
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
| `Init()` | `ResultStatus` | Checks that each buffer is DMA-capable (`CheckDmaBuffer`), calls `lv_init()`, sets the tick, creates the display, and registers the buffers and the flush and flush-wait callbacks. Call once, after the panel is initialised |
| `TimerHandler()` | `uint32` | Runs LVGL timers and redraws. Returns the milliseconds until the next call is due |
| `WaitIdle()` | `void` | Waits until the last background write has finished and marks it ready. Call before reading the panel or reusing the buffers outside LVGL |
| `GetDisplay()` | `lv_display_t*` | The LVGL display, for `lv_refr_now` and similar calls |
| `GetLastFlushStatus()` | `ResultStatus` | Result of the last bus transfer made for LVGL, or of the last wait that failed |
| `Flush(display, area, pixels)` | `void` (static, public) | The LVGL flush callback. Public so a test can wrap it. Do not call it directly |
| `WaitFlush(display)` | `void` (static, public) | The LVGL flush-wait callback. Public for the same reason |

---

## Flush Path

LVGL renders into the band buffer in `LV_COLOR_FORMAT_RGB565_SWAPPED`, which is the panel's byte order.

With `BufferCount` 1, the flush callback calls `SetWindow`, then `WritePixelBytes` with the area's byte count (two per pixel), then `lv_display_flush_ready`. The transfer is synchronous.

With `BufferCount` 2, the flush callback calls `SetWindow` and then `WritePixelBytesAsync`. It does not signal ready. LVGL renders the next band into the other buffer. Before LVGL reuses a buffer that is still being written, it calls the flush-wait callback, which waits for the write and returns; LVGL then clears its busy state itself. If the port cannot write in the background (`notSupported`), the flush falls back to `WritePixelBytes` and signals ready at once.

The flush-wait callback is the only place where the bridge blocks. It waits up to `flushTimeoutMs` (1000 ms). On timeout the error is recorded and the buffer is handed over anyway, so the UI keeps running, but the affected band may be wrong.

The panel must not see the data/command line change while a background write is running. `ST7789` waits for the write before any command or data, and `SetWindow` is synchronous, so the window commands (CASET, RASET, RAMWR) reach the panel before the pixel data of the same flush. The next flush cannot start until the callbacks have collected the previous write.

The callback reaches the panel through the bridge pointer stored with `lv_display_set_user_data`. A failed transfer is recorded in `GetLastFlushStatus()`.

The tick is `System::GetMs()` truncated to 32 bits, in milliseconds. The system tick therefore needs to run at 1 kHz for LVGL's timing to be correct.

---

## DMA-Capable Buffers

Every draw buffer must lie in memory the port's bus can read. `Init()` checks this through `ST7789::CheckDmaBuffer`, which the SPI port answers. On the ESP32 port, the check uses `esp_ptr_dma_capable()`, and returns `invalidParameter` for a buffer outside internal RAM. `Init()` then fails instead of corrupting the screen.

The rule, as the ESP-IDF memory guide states it (`docs/en/api-reference/system/mem_alloc.rst`): "Use the `MALLOC_CAP_DMA` flag to allocate memory which is suitable for use with hardware DMA engines (for example SPI and I2S). This capability flag excludes any external PSRAM."

On the classic ESP32, the SPI DMA cannot read PSRAM. Static arrays such as the draw buffers are placed in internal DRAM. The check is there so a later change that moves them fails at `Init()`.

---

## Requirements

- LVGL 9. The bridge uses `lv_display_set_buffers` with `LV_DISPLAY_RENDER_MODE_PARTIAL`, `lv_display_set_flush_wait_cb` and `lv_display_flush_ready`, which are LVGL 9 names.
- A `ST7789` instance that outlives the bridge.
- `System::Init()` and a running system tick before `Init()`.
- Draw buffers in DMA-capable memory (see above).
