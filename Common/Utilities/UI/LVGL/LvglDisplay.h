#pragma once
#include <VHAL.h>

#if defined(VHAL_LVGL)
#include <Drivers/Display/IDisplay.h>
#include <System/System.h>
#include <Utilities/Result/Result.h>
#include <lvgl.h>
#include <span>


// Connects LVGL 9 to any display behind IDisplay in partial render mode: LVGL draws into a band of lineCount lines and
// every filled area is flushed to the panel. The caller allocates the draw buffers (one band, or two for background
// flushes) and keeps them alive as long as the bridge. LVGL itself is an external dependency: the project adds it and
// defines VHAL_LVGL in its VHALConfig.h. This header includes only LVGL's public header.
class LvglDisplay {
public:
	static constexpr uint32 flushTimeoutMs = 1000;


private:
	IDisplay& panel;
	uint16 lineCount = 0;
	std::span<uint8> firstBuffer;
	std::span<uint8> secondBuffer;
	lv_display_t* display = nullptr;
	ResultStatus lastFlushStatus = ResultStatus::ok;


public:
	// Bytes of one draw buffer: a band of lines for a panel of the given width, two bytes a pixel.
	static constexpr uint32 BufferBytes(uint16 width, uint16 lines) {
		return static_cast<uint32>(width) * lines * 2;
	}


	// An empty secondBuffer selects the synchronous flush with one buffer.
	LvglDisplay(
		IDisplay& panelDriver,
		uint16 lines,
		std::span<uint8> buffer,
		std::span<uint8> secondDrawBuffer = {}
	)
		: panel(panelDriver), lineCount(lines), firstBuffer(buffer), secondBuffer(secondDrawBuffer)
	{
	}


	// Call once, after the panel is initialised and System::Init() has run. Fails when a buffer is smaller than one band
	// of the panel's width, or when a buffer is not in memory the bus can read (IDisplay::CheckDmaBuffer).
	ResultStatus Init() {
		if (lineCount == 0) {
			return ResultStatus::invalidParameter;
		}

		uint32 bandBytes = BufferBytes(panel.GetWidth(), lineCount);
		bool hasSecond = !secondBuffer.empty();
		if (firstBuffer.size() < bandBytes || (hasSecond && secondBuffer.size() < bandBytes)) {
			return ResultStatus::invalidParameter;
		}

		ResultStatus status = panel.CheckDmaBuffer(firstBuffer.data(), bandBytes);
		if (status != ResultStatus::ok) {
			return status;
		}
		if (hasSecond) {
			status = panel.CheckDmaBuffer(secondBuffer.data(), bandBytes);
			if (status != ResultStatus::ok) {
				return status;
			}
		}

		// The only panel format so far, RGB565 with the high byte first, is LVGL's swapped RGB565. Another panel format
		// needs its own LVGL format here.
		if (panel.GetPixelFormat() != IDisplay::PixelFormat::Rgb565BigEndian) {
			return ResultStatus::notSupported;
		}

		lv_init();
		lv_tick_set_cb(&LvglDisplay::Tick);

		display = lv_display_create(panel.GetWidth(), panel.GetHeight());
		if (display == nullptr) {
			return ResultStatus::error;
		}

		lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565_SWAPPED);
		lv_display_set_buffers(display, firstBuffer.data(), hasSecond ? secondBuffer.data() : nullptr, bandBytes,
			LV_DISPLAY_RENDER_MODE_PARTIAL);
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
			if (bridge->secondBuffer.empty()) {
				status = bridge->panel.WritePixelBytes(pixelMap, bytes);
			} else {
				status = bridge->panel.WritePixelBytesAsync(pixelMap, bytes);
				if (status == ResultStatus::ok) {
					pending = true;
				} else if (status == ResultStatus::notSupported) {
					status = bridge->panel.WritePixelBytes(pixelMap, bytes);
				}
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

#endif
