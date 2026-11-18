#pragma once
#include <System/System.h>
#include <Drivers/Display/ST7789/ST7789.h>
#include <Utilities/Result/Result.h>
#include <lvgl.h>


// Connects LVGL 9 to an ST7789 panel in partial render mode: LVGL draws into a band of LineCount lines and every filled
// area is flushed through the panel. LVGL itself is an external dependency added by the project; this bridge only
// includes its public header. The buffers are RGB565 with the bytes swapped (LV_COLOR_FORMAT_RGB565_SWAPPED), which is
// the panel's wire order, so a flush sends the buffer as it is. With BufferCount 2 the flush starts a write and returns,
// and LVGL renders the next band into the other buffer while the first one is written.
template<uint16 LineCount = 20, uint16 MaxWidth = 135, uint8 BufferCount = 1>
class LvglDisplay {
public:
	static constexpr uint32 bufferBytes = static_cast<uint32>(MaxWidth) * LineCount * 2;
	static constexpr uint32 flushTimeoutMs = 1000;

	static_assert(BufferCount == 1 || BufferCount == 2, "LvglDisplay supports one or two draw buffers");


private:
	alignas(4) static inline uint8 drawBuffers[BufferCount][bufferBytes];

	ST7789& panel;
	lv_display_t* display = nullptr;
	ResultStatus lastFlushStatus = ResultStatus::ok;


public:
	explicit LvglDisplay(ST7789& panelDriver): panel(panelDriver) { }


	// Call once, after the panel is initialised and System::Init() has run. Fails when the panel is wider than
	// MaxWidth, because the buffers are sized for MaxWidth, and when a buffer is not in memory the bus can read
	// (CheckDmaBuffer).
	ResultStatus Init() {
		if (panel.GetWidth() > MaxWidth) {
			return ResultStatus::outOfRange;
		}
		for (uint8 index = 0; index < BufferCount; index++) {
			ResultStatus status = panel.CheckDmaBuffer(drawBuffers[index], bufferBytes);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		lv_init();
		lv_tick_set_cb(&LvglDisplay::Tick);

		display = lv_display_create(panel.GetWidth(), panel.GetHeight());
		if (display == nullptr) {
			return ResultStatus::error;
		}

		lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
		lv_display_set_buffers(display, drawBuffers[0], BufferCount > 1 ? drawBuffers[BufferCount - 1] : nullptr,
			bufferBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
		lv_display_set_flush_cb(display, &LvglDisplay::Flush);
		lv_display_set_flush_wait_cb(display, &LvglDisplay::WaitFlush);
		lv_display_set_user_data(display, this);
		return ResultStatus::ok;
	}


	// Runs LVGL's timers and redraws. Returns the time until the next call is due, in milliseconds.
	uint32 TimerHandler() {
		return lv_timer_handler();
	}


	// Waits until the last flush has finished and marks it ready. Call it before the panel is read or the buffers are
	// reused outside LVGL.
	void WaitIdle() {
		if (display != nullptr) {
			WaitFlush(display);
			lv_display_flush_ready(display);
		}
	}


	lv_display_t* GetDisplay() const {
		return display;
	}


	// Result of the last bus transfer made for LVGL. A failed flush still signals ready, so the UI keeps running.
	ResultStatus GetLastFlushStatus() const {
		return lastFlushStatus;
	}


	// LVGL flush callback. Public so that a test can wrap it, not for direct calls: it signals ready itself unless the
	// write was started asynchronously, in which case WaitFlush does it through LVGL.
	static void Flush(lv_display_t* target, const lv_area_t* area, uint8_t* pixelMap) {
		LvglDisplay* bridge = static_cast<LvglDisplay*>(lv_display_get_user_data(target));
		uint32 pixels = static_cast<uint32>(lv_area_get_width(area)) * static_cast<uint32>(lv_area_get_height(area));
		uint32 bytes = pixels * 2;
		bool pending = false;

		ResultStatus status = bridge->panel.SetWindow(
			static_cast<uint16>(area->x1),
			static_cast<uint16>(area->y1),
			static_cast<uint16>(area->x2),
			static_cast<uint16>(area->y2)
		);
		if (status == ResultStatus::ok) {
			if (BufferCount > 1) {
				status = bridge->panel.WritePixelBytesAsync(pixelMap, bytes);
				if (status == ResultStatus::ok) {
					pending = true;
				} else if (status == ResultStatus::notSupported) {
					status = bridge->panel.WritePixelBytes(pixelMap, bytes);
				}
			} else {
				status = bridge->panel.WritePixelBytes(pixelMap, bytes);
			}
		}
		bridge->lastFlushStatus = status;

		if (!pending) {
			lv_display_flush_ready(target);
		}
	}


	// LVGL flush-wait callback. LVGL calls it only while a flush is still running, before it reuses that buffer. Waits
	// up to flushTimeoutMs; on timeout the error is recorded, and the buffer is handed over anyway so the UI keeps running.
	static void WaitFlush(lv_display_t* target) {
		LvglDisplay* bridge = static_cast<LvglDisplay*>(lv_display_get_user_data(target));
		ResultStatus status = bridge->panel.WaitWriteDone(flushTimeoutMs);
		if (status != ResultStatus::ok) {
			bridge->lastFlushStatus = status;
		}
	}


private:
	static uint32 Tick() {
		return static_cast<uint32>(System::GetMs());
	}
};
