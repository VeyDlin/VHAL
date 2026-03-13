#pragma once
#include <Adapter/Port/ESP32/Adapter/DSIAdapter.h>


using ADSI = class DSIAdapterESP;


class DSIAdapterESP : public DSIAdapter {
public:
	struct ClockSource {
		static inline constexpr ClockSourceOption Default   { MIPI_DSI_PHY_CLK_SRC_DEFAULT };
		static inline constexpr ClockSourceOption RcFast    { MIPI_DSI_PHY_CLK_SRC_RC_FAST };
		static inline constexpr ClockSourceOption PllF25M   { MIPI_DSI_PHY_CLK_SRC_PLL_F25M };
		static inline constexpr ClockSourceOption PllF20M   { MIPI_DSI_PHY_CLK_SRC_PLL_F20M };
	};

	struct ColorFormat {
		static inline constexpr ColorFormatOption RGB565    { LCD_COLOR_PIXEL_FORMAT_RGB565 };
		static inline constexpr ColorFormatOption RGB666    { LCD_COLOR_PIXEL_FORMAT_RGB666 };
		static inline constexpr ColorFormatOption RGB888    { LCD_COLOR_PIXEL_FORMAT_RGB888 };
	};

	struct DataEndian {
		static inline constexpr DataEndianOption Big    { LCD_RGB_DATA_ENDIAN_BIG };
		static inline constexpr DataEndianOption Little { LCD_RGB_DATA_ENDIAN_LITTLE };
	};

	struct ColorSpace {
		static inline constexpr ColorSpaceOption RGB { LCD_COLOR_SPACE_RGB };
		static inline constexpr ColorSpaceOption YUV { LCD_COLOR_SPACE_YUV };
	};

	struct ColorRange {
		static inline constexpr ColorRangeOption Limited { LCD_COLOR_RANGE_LIMIT };
		static inline constexpr ColorRangeOption Full    { LCD_COLOR_RANGE_FULL };
	};

	struct YuvSample {
		static inline constexpr YuvSampleOption S422 { LCD_YUV_SAMPLE_422 };
		static inline constexpr YuvSampleOption S420 { LCD_YUV_SAMPLE_420 };
		static inline constexpr YuvSampleOption S411 { LCD_YUV_SAMPLE_411 };
	};

	struct YuvConvStd {
		static inline constexpr YuvConvStdOption BT601 { LCD_YUV_CONV_STD_BT601 };
		static inline constexpr YuvConvStdOption BT709 { LCD_YUV_CONV_STD_BT709 };
	};

	struct YuvPackOrder {
		static inline constexpr YuvPackOrderOption YUYV { LCD_YUV422_PACK_ORDER_YUYV };
		static inline constexpr YuvPackOrderOption YVYU { LCD_YUV422_PACK_ORDER_YVYU };
		static inline constexpr YuvPackOrderOption UYVY { LCD_YUV422_PACK_ORDER_UYVY };
		static inline constexpr YuvPackOrderOption VYUY { LCD_YUV422_PACK_ORDER_VYUY };
	};

	struct FrameBufferCount {
		static inline constexpr FrameBufferCountOption Single { 1 };
		static inline constexpr FrameBufferCountOption Double { 2 };
		static inline constexpr FrameBufferCountOption Triple { 3 };
	};

	struct Config {
		bool useDma2d = false;
		DataEndianOption dataEndian = DataEndian::Big;
		ColorSpaceOption colorSpace = ColorSpace::RGB;
		ColorRangeOption colorRange = ColorRange::Full;
	};


protected:
	int busId = 0;


public:
	Config config;


	DSIAdapterESP() = default;

	DSIAdapterESP(int busId) : busId(busId) { }

	~DSIAdapterESP() {
		Deinit();
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus WriteCommandBytes(uint8 cmd, const uint8 *params, uint32 paramSize) override {
		if (!io) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_io_tx_param(io, cmd, params, paramSize) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus ReadCommandBytes(uint8 cmd, uint8 *data, uint32 size) override {
		if (!io) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_io_rx_param(io, cmd, data, size) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus Start() override {
		if (parameters.mode != Mode::Video) {
			return ResultStatus::ok;
		}
		if (panel) {
			return ResultStatus::ok;
		}
		if (!bus) {
			return ResultStatus::error;
		}

		auto status = InitDpiPanel();
		if (status != ResultStatus::ok) {
			return status;
		}

		RegisterCallbacks();
		return ResultStatus::ok;
	}


	ResultStatus Stop() override {
		if (panel) {
			esp_lcd_panel_del(panel);
			panel = nullptr;
		}
		for (auto &fb : frameBuffers) {
			fb = nullptr;
		}
		return ResultStatus::ok;
	}


	void* GetFrameBuffer(uint8 index) override {
		if (index >= parameters.frameBufferCount.Get()) {
			return nullptr;
		}
		return frameBuffers[index];
	}


	ResultStatus DrawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void *colorData) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_draw_bitmap(panel, xStart, yStart, xEnd, yEnd, colorData) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus Mirror(bool mirrorX, bool mirrorY) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_mirror(panel, mirrorX, mirrorY) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus SwapXY(bool swap) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_swap_xy(panel, swap) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus SetGap(int xGap, int yGap) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_set_gap(panel, xGap, yGap) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus InvertColor(bool invert) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_invert_color(panel, invert) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus DisplayOnOff(bool on) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_disp_on_off(panel, on) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus DisplaySleep(bool sleep) override {
		if (!panel) {
			return ResultStatus::error;
		}
		if (esp_lcd_panel_disp_sleep(panel, sleep) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	void IrqHandler() override { }


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		Deinit();

		// DSI bus
		esp_lcd_dsi_bus_config_t bus_cfg = {};
		bus_cfg.bus_id = busId;
		bus_cfg.num_data_lanes = parameters.laneCount;
		bus_cfg.phy_clk_src = static_cast<mipi_dsi_phy_pllref_clock_source_t>(parameters.clockSource.Get());
		bus_cfg.lane_bit_rate_mbps = parameters.laneBitRateMbps;

		if (esp_lcd_new_dsi_bus(&bus_cfg, &bus) != ESP_OK) {
			return ResultStatus::error;
		}

		// DBI IO (for DCS commands)
		esp_lcd_dbi_io_config_t io_cfg = {};
		io_cfg.virtual_channel = parameters.virtualChannel;
		io_cfg.lcd_cmd_bits = 8;
		io_cfg.lcd_param_bits = 8;

		if (esp_lcd_new_panel_io_dbi(bus, &io_cfg, &io) != ESP_OK) {
			Deinit();
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


private:
	lcd_color_rgb_pixel_format_t CastPixelFormat() const {
		return static_cast<lcd_color_rgb_pixel_format_t>(parameters.colorFormat.Get());
	}


	ResultStatus InitDpiPanel() {
		esp_lcd_dpi_panel_config_t dpi_cfg = {};
		dpi_cfg.virtual_channel = parameters.virtualChannel;
		dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
		dpi_cfg.dpi_clock_freq_mhz = parameters.pixelClockKHz / 1000;
		dpi_cfg.pixel_format = CastPixelFormat();
		dpi_cfg.num_fbs = parameters.frameBufferCount.Get();

		dpi_cfg.video_timing.h_size = parameters.timing.activeWidth;
		dpi_cfg.video_timing.v_size = parameters.timing.activeHeight;
		dpi_cfg.video_timing.hsync_back_porch = parameters.timing.hBackPorch;
		dpi_cfg.video_timing.hsync_front_porch = parameters.timing.hFrontPorch;
		dpi_cfg.video_timing.hsync_pulse_width = parameters.timing.hsyncWidth;
		dpi_cfg.video_timing.vsync_back_porch = parameters.timing.vBackPorch;
		dpi_cfg.video_timing.vsync_front_porch = parameters.timing.vFrontPorch;
		dpi_cfg.video_timing.vsync_pulse_width = parameters.timing.vsyncWidth;

		dpi_cfg.flags.use_dma2d = config.useDma2d;

		if (esp_lcd_new_panel_dpi(bus, &dpi_cfg, &panel) != ESP_OK) {
			return ResultStatus::error;
		}

		if (esp_lcd_panel_init(panel) != ESP_OK) {
			esp_lcd_panel_del(panel);
			panel = nullptr;
			return ResultStatus::error;
		}

		// Cache framebuffer pointers
		uint8 fbCount = parameters.frameBufferCount.Get();
		switch (fbCount) {
			case 1:
				esp_lcd_dpi_panel_get_frame_buffer(panel, 1, &frameBuffers[0]);
				break;
			case 2:
				esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &frameBuffers[0], &frameBuffers[1]);
				break;
			case 3:
				esp_lcd_dpi_panel_get_frame_buffer(panel, 3, &frameBuffers[0], &frameBuffers[1], &frameBuffers[2]);
				break;
		}

		return ResultStatus::ok;
	}


	void RegisterCallbacks() {
		if (!panel) {
			return;
		}

		esp_lcd_dpi_panel_event_callbacks_t cbs = {};
		cbs.on_color_trans_done = OnTransferDone;
		cbs.on_refresh_done = OnRefreshDone;
		esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, this);
	}


	IRAM_ATTR static bool OnTransferDone(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
		auto *self = static_cast<DSIAdapterESP*>(user_ctx);
		self->CallInterrupt(Irq::TransferDone);
		return false;
	}


	IRAM_ATTR static bool OnRefreshDone(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *user_ctx) {
		auto *self = static_cast<DSIAdapterESP*>(user_ctx);
		self->CallInterrupt(Irq::RefreshDone);
		return false;
	}


	void Deinit() {
		if (panel) {
			esp_lcd_panel_del(panel);
			panel = nullptr;
		}
		if (io) {
			esp_lcd_panel_io_del(io);
			io = nullptr;
		}
		if (bus) {
			esp_lcd_del_dsi_bus(bus);
			bus = nullptr;
		}
		for (auto &fb : frameBuffers) {
			fb = nullptr;
		}
	}
};
