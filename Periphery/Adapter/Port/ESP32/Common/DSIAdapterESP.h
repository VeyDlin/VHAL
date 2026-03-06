#pragma once
#include <Adapter/DSIAdapter.h>


using ADSI = class DSIAdapterESP;


class DSIAdapterESP : public DSIAdapter<void> {
public:
	struct ClockSource {
		static inline constexpr ClockSourceOption Default { MIPI_DSI_PHY_CLK_SRC_DEFAULT };
	};

	struct ColorFormat {
		static inline constexpr ColorFormatOption RGB565        { LCD_COLOR_PIXEL_FORMAT_RGB565 };
		static inline constexpr ColorFormatOption RGB666        { LCD_COLOR_PIXEL_FORMAT_RGB666 };
		static inline constexpr ColorFormatOption RGB888        { LCD_COLOR_PIXEL_FORMAT_RGB888 };
	};

	struct Config {
		bool useDma2d = false;
		bool disableLp = false;
	};


public:
	DSIAdapterESP() = default;

	DSIAdapterESP(int busId, Config config = {})
		: busId(busId), config(config) { }

	~DSIAdapterESP() {
		Deinit();
	}


	Status::statusType SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	Status::statusType WriteCommand(uint8 cmd, const uint8 *params, uint32 paramSize) override {
		if (!ioHandle) {
			return Status::error;
		}
		if (esp_lcd_panel_io_tx_param(ioHandle, cmd, params, paramSize) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType ReadCommand(uint8 cmd, uint8 *data, uint32 size) override {
		if (!ioHandle) {
			return Status::error;
		}
		if (esp_lcd_panel_io_rx_param(ioHandle, cmd, data, size) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType Start() override {
		if (!panelHandle) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType Stop() override {
		Deinit();
		return Status::ok;
	}


	void* GetFrameBuffer(uint8 index) override {
		if (index >= parameters.frameBufferCount) {
			return nullptr;
		}
		return frameBuffers[index];
	}


	void IrqHandler() override { }


protected:
	int busId = 0;
	Config config;
	esp_lcd_dsi_bus_handle_t busHandle = nullptr;
	esp_lcd_panel_io_handle_t ioHandle = nullptr;
	esp_lcd_panel_handle_t panelHandle = nullptr;
	void *frameBuffers[3] = {};


	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		Deinit();

		// DSI bus
		esp_lcd_dsi_bus_config_t bus_cfg = {};
		bus_cfg.bus_id = busId;
		bus_cfg.num_data_lanes = parameters.laneCount;
		bus_cfg.phy_clk_src = static_cast<mipi_dsi_phy_pllref_clock_source_t>(parameters.clockSource.Get());
		bus_cfg.lane_bit_rate_mbps = parameters.laneBitRateMbps;

		if (esp_lcd_new_dsi_bus(&bus_cfg, &busHandle) != ESP_OK) {
			return Status::error;
		}

		// DBI IO (for DCS commands)
		esp_lcd_dbi_io_config_t io_cfg = {};
		io_cfg.virtual_channel = parameters.virtualChannel;
		io_cfg.lcd_cmd_bits = 8;
		io_cfg.lcd_param_bits = 8;

		if (esp_lcd_new_panel_io_dbi(busHandle, &io_cfg, &ioHandle) != ESP_OK) {
			Deinit();
			return Status::error;
		}

		// DPI panel (video mode)
		if (parameters.mode == Mode::Video) {
			status = InitDpiPanel();
			if (status != Status::ok) {
				Deinit();
				return status;
			}
		}

		return AfterInitialization();
	}


private:
	lcd_color_rgb_pixel_format_t CastPixelFormat() const {
		return static_cast<lcd_color_rgb_pixel_format_t>(parameters.colorFormat.Get());
	}


	Status::statusType InitDpiPanel() {
		esp_lcd_dpi_panel_config_t dpi_cfg = {};
		dpi_cfg.virtual_channel = parameters.virtualChannel;
		dpi_cfg.dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
		dpi_cfg.dpi_clock_freq_mhz = parameters.pixelClockKHz / 1000;
		dpi_cfg.pixel_format = CastPixelFormat();
		dpi_cfg.num_fbs = parameters.frameBufferCount;

		dpi_cfg.video_timing.h_size = parameters.timing.activeWidth;
		dpi_cfg.video_timing.v_size = parameters.timing.activeHeight;
		dpi_cfg.video_timing.hsync_back_porch = parameters.timing.hBackPorch;
		dpi_cfg.video_timing.hsync_front_porch = parameters.timing.hFrontPorch;
		dpi_cfg.video_timing.hsync_pulse_width = parameters.timing.hsyncWidth;
		dpi_cfg.video_timing.vsync_back_porch = parameters.timing.vBackPorch;
		dpi_cfg.video_timing.vsync_front_porch = parameters.timing.vFrontPorch;
		dpi_cfg.video_timing.vsync_pulse_width = parameters.timing.vsyncWidth;

		dpi_cfg.flags.use_dma2d = config.useDma2d;

		if (esp_lcd_new_panel_dpi(busHandle, &dpi_cfg, &panelHandle) != ESP_OK) {
			return Status::error;
		}

		// Cache framebuffer pointers
		switch (parameters.frameBufferCount) {
			case 1:
				esp_lcd_dpi_panel_get_frame_buffer(panelHandle, 1, &frameBuffers[0]);
				break;
			case 2:
				esp_lcd_dpi_panel_get_frame_buffer(panelHandle, 2, &frameBuffers[0], &frameBuffers[1]);
				break;
			case 3:
				esp_lcd_dpi_panel_get_frame_buffer(panelHandle, 3, &frameBuffers[0], &frameBuffers[1], &frameBuffers[2]);
				break;
		}

		return Status::ok;
	}


	void Deinit() {
		if (panelHandle) {
			esp_lcd_panel_del(panelHandle);
			panelHandle = nullptr;
		}
		if (ioHandle) {
			esp_lcd_panel_io_del(ioHandle);
			ioHandle = nullptr;
		}
		if (busHandle) {
			esp_lcd_del_dsi_bus(busHandle);
			busHandle = nullptr;
		}
		for (auto &fb : frameBuffers) {
			fb = nullptr;
		}
	}
};
