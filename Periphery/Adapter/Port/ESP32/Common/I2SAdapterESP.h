#pragma once
#include <Adapter/I2SAdapter.h>


using AI2S = class I2SAdapterESP;


class I2SAdapterESP : public I2SAdapter<void> {
public:
	struct ClockSource {
		static inline constexpr ClockSourceOption Default { I2S_CLK_SRC_DEFAULT };
#if SOC_I2S_SUPPORTS_APLL
		static inline constexpr ClockSourceOption Apll { I2S_CLK_SRC_APLL };
#endif
	};

	struct SlotCount {
		static inline constexpr SlotCountOption S1  { 1 };
		static inline constexpr SlotCountOption S2  { 2 };
		static inline constexpr SlotCountOption S4  { 4 };
		static inline constexpr SlotCountOption S8  { 8 };
		static inline constexpr SlotCountOption S16 { 16 };
	};

	struct Slot {
		static inline constexpr SlotOption S0  { 1u << 0 };
		static inline constexpr SlotOption S1  { 1u << 1 };
		static inline constexpr SlotOption S2  { 1u << 2 };
		static inline constexpr SlotOption S3  { 1u << 3 };
		static inline constexpr SlotOption S4  { 1u << 4 };
		static inline constexpr SlotOption S5  { 1u << 5 };
		static inline constexpr SlotOption S6  { 1u << 6 };
		static inline constexpr SlotOption S7  { 1u << 7 };
		static inline constexpr SlotOption S8  { 1u << 8 };
		static inline constexpr SlotOption S9  { 1u << 9 };
		static inline constexpr SlotOption S10 { 1u << 10 };
		static inline constexpr SlotOption S11 { 1u << 11 };
		static inline constexpr SlotOption S12 { 1u << 12 };
		static inline constexpr SlotOption S13 { 1u << 13 };
		static inline constexpr SlotOption S14 { 1u << 14 };
		static inline constexpr SlotOption S15 { 1u << 15 };

		static inline constexpr SlotOption Left   { 1u << 0 };
		static inline constexpr SlotOption Right  { 1u << 1 };

		static inline constexpr SlotOption Mono   { 1u << 0 };
		static inline constexpr SlotOption Stereo { (1u << 0) | (1u << 1) };
	};

	// PDM mode clocks on gpio.bclk when it is set, otherwise on gpio.ws.
	struct GpioConfig {
		int mclk = -1;
		int bclk = -1;
		int ws = -1;
		int dataOut = -1;
		int dataIn = -1;
	};

	struct DmaConfig {
		uint32 bufferCount = 6;
		uint32 frameCount = 240;
	};


public:
	I2SAdapterESP() = default;

	I2SAdapterESP(i2s_port_t port, GpioConfig gpio)
		: I2SAdapterESP(port, gpio, DmaConfig()) { }

	I2SAdapterESP(i2s_port_t port, GpioConfig gpio, DmaConfig dma)
		: port(port), gpio(gpio), dma(dma) { }

	~I2SAdapterESP() {
		Deinit();
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus Transmit(uint16 *buffer, uint32 size) override {
		Result<uint32> result = TransmitBytes(reinterpret_cast<const uint8 *>(buffer), size * sizeof(uint16));
		if (result.IsErr()) {
			return result.Error();
		}
		if (result.Value() < size * sizeof(uint16)) {
			return ResultStatus::timeout;
		}
		return ResultStatus::ok;
	}


	ResultStatus Receive(uint16 *buffer, uint32 size) override {
		Result<uint32> result = ReceiveBytes(reinterpret_cast<uint8 *>(buffer), size * sizeof(uint16));
		if (result.IsErr()) {
			return result.Error();
		}
		if (result.Value() < size * sizeof(uint16)) {
			return ResultStatus::timeout;
		}
		return ResultStatus::ok;
	}


	// Returns the number of bytes actually written; a short count on timeout is still a success.
	Result<uint32> TransmitBytes(const uint8 *buffer, uint32 size) {
		if (!txHandle) {
			return ResultStatus::noInit;
		}
		ResultStatus status = EnableChannel(txHandle, txEnabled);
		if (status != ResultStatus::ok) {
			return status;
		}
		size_t bytesWritten = 0;
		esp_err_t err = i2s_channel_write(txHandle, buffer, size, &bytesWritten, timeout);
		if (err != ESP_OK && bytesWritten == 0) {
			return (err == ESP_ERR_TIMEOUT) ? ResultStatus::timeout : ResultStatus::error;
		}
		return static_cast<uint32>(bytesWritten);
	}


	// Returns the number of bytes actually read; a short count on timeout is still a success.
	Result<uint32> ReceiveBytes(uint8 *buffer, uint32 size) {
		if (!rxHandle) {
			return ResultStatus::noInit;
		}
		ResultStatus status = EnableChannel(rxHandle, rxEnabled);
		if (status != ResultStatus::ok) {
			return status;
		}
		size_t bytesRead = 0;
		esp_err_t err = i2s_channel_read(rxHandle, buffer, size, &bytesRead, timeout);
		if (err != ESP_OK && bytesRead == 0) {
			return (err == ESP_ERR_TIMEOUT) ? ResultStatus::timeout : ResultStatus::error;
		}
		return static_cast<uint32>(bytesRead);
	}


	ResultStatus TransmitReceive(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) override {
		return ResultStatus::notSupported;
	}


	ResultStatus TransmitAsync(uint16 *buffer, uint32 size) override {
		return ResultStatus::notSupported;
	}

	ResultStatus ReceiveAsync(uint16 *buffer, uint32 size) override {
		return ResultStatus::notSupported;
	}

	ResultStatus TransmitReceiveAsync(uint16 *txBuffer, uint16 *rxBuffer, uint32 size) override {
		return ResultStatus::notSupported;
	}


	ResultStatus TransmitCircular(uint16 *buffer, uint32 size) override {
		return ResultStatus::notSupported;
	}

	ResultStatus ReceiveCircular(uint16 *buffer, uint32 size) override {
		return ResultStatus::notSupported;
	}


	ResultStatus StopTransmit() override {
		if (!txHandle) {
			return ResultStatus::noInit;
		}
		return DisableChannel(txHandle, txEnabled);
	}

	ResultStatus StopReceive() override {
		if (!rxHandle) {
			return ResultStatus::noInit;
		}
		return DisableChannel(rxHandle, rxEnabled);
	}


	void IrqHandler() override { }


protected:
	i2s_port_t port = I2S_NUM_AUTO;
	GpioConfig gpio;
	DmaConfig dma;
	i2s_chan_handle_t txHandle = nullptr;
	i2s_chan_handle_t rxHandle = nullptr;
	bool txEnabled = false;
	bool rxEnabled = false;


	ResultStatus Initialization() override {
		ResultStatus status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		// Checked before the teardown, so a rejected request leaves the current configuration running.
		status = ValidateMode();
		if (status != ResultStatus::ok) {
			return status;
		}

		Deinit();

		i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(port, CastRole());
		chan_cfg.dma_desc_num = dma.bufferCount;
		chan_cfg.dma_frame_num = dma.frameCount;

		i2s_chan_handle_t *txPtr = nullptr;
		i2s_chan_handle_t *rxPtr = nullptr;

		if (parameters.direction == Direction::Tx || parameters.direction == Direction::TxRx) {
			txPtr = &txHandle;
		}
		if (parameters.direction == Direction::Rx || parameters.direction == Direction::TxRx) {
			rxPtr = &rxHandle;
		}

		if (i2s_new_channel(&chan_cfg, txPtr, rxPtr) != ESP_OK) {
			return ResultStatus::error;
		}

		if (parameters.standard == Standard::PDM) {
			status = InitPdmMode();
		} else if (parameters.slotCount.Get() > 2) {
#if SOC_I2S_SUPPORTS_TDM
			status = InitTdmMode();
#else
			status = ResultStatus::notSupported;
#endif
		} else {
			status = InitStdMode();
		}

		if (status != ResultStatus::ok) {
			Deinit();
			return status;
		}

		if (txHandle) {
			if (i2s_channel_enable(txHandle) != ESP_OK) {
				Deinit();
				return ResultStatus::error;
			}
			txEnabled = true;
		}

		if (rxHandle) {
			if (i2s_channel_enable(rxHandle) != ESP_OK) {
				Deinit();
				return ResultStatus::error;
			}
			rxEnabled = true;
		}

		return AfterInitialization();
	}


private:
	i2s_role_t CastRole() const {
		return (parameters.mode == Mode::Master) ? I2S_ROLE_MASTER : I2S_ROLE_SLAVE;
	}

	i2s_data_bit_width_t CastDataBitWidth() const {
		switch (parameters.dataFormat) {
			case DataFormat::B16:         return I2S_DATA_BIT_WIDTH_16BIT;
			case DataFormat::B16Extended: return I2S_DATA_BIT_WIDTH_16BIT;
			case DataFormat::B24:         return I2S_DATA_BIT_WIDTH_24BIT;
			case DataFormat::B32:         return I2S_DATA_BIT_WIDTH_32BIT;
		}
		return I2S_DATA_BIT_WIDTH_16BIT;
	}

	i2s_slot_bit_width_t CastSlotBitWidth() const {
		if (parameters.dataFormat == DataFormat::B16Extended) {
			return I2S_SLOT_BIT_WIDTH_32BIT;
		}
		return I2S_SLOT_BIT_WIDTH_AUTO;
	}

	i2s_slot_mode_t CastSlotMode() const {
		return (parameters.slotCount.Get() <= 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
	}

	i2s_clock_src_t CastClockSource() const {
		if (parameters.clockSource.Get() != 0) {
			return static_cast<i2s_clock_src_t>(parameters.clockSource.Get());
		}
		return I2S_CLK_SRC_DEFAULT;
	}

	gpio_num_t CastGpio(int pin) const {
		if (pin < 0) {
			return I2S_GPIO_UNUSED;
		}
		return static_cast<gpio_num_t>(pin);
	}

	i2s_pdm_slot_mask_t CastPdmSlotMask() const {
		uint32 slots = parameters.activeSlots.Get();
		uint32 mask = 0;
		if ((slots & Slot::Left.Get()) != 0) {
			mask |= I2S_PDM_SLOT_LEFT;
		}
		if ((slots & Slot::Right.Get()) != 0) {
			mask |= I2S_PDM_SLOT_RIGHT;
		}
		return static_cast<i2s_pdm_slot_mask_t>(mask);
	}

	ResultStatus EnableChannel(i2s_chan_handle_t handle, bool& enabled) {
		if (enabled) {
			return ResultStatus::ok;
		}
		if (i2s_channel_enable(handle) != ESP_OK) {
			return ResultStatus::error;
		}
		enabled = true;
		return ResultStatus::ok;
	}

	ResultStatus DisableChannel(i2s_chan_handle_t handle, bool& enabled) {
		if (!enabled) {
			return ResultStatus::ok;
		}
		if (i2s_channel_disable(handle) != ESP_OK) {
			return ResultStatus::error;
		}
		enabled = false;
		return ResultStatus::ok;
	}

	// Rejects unsupported requests before any channel is created.
	ResultStatus ValidateMode() const {
		if (parameters.standard == Standard::PDM) {
			if (parameters.dataFormat != DataFormat::B16 || parameters.direction == Direction::TxRx) {
				return ResultStatus::notSupported;
			}
			// The ESP-IDF driver runs PDM on the first I2S port only.
			if (port != I2S_NUM_0 && port != I2S_NUM_AUTO) {
				return ResultStatus::notSupported;
			}
#if !SOC_I2S_SUPPORTS_PDM_RX
			if (parameters.direction == Direction::Rx) {
				return ResultStatus::notSupported;
			}
#endif
#if !SOC_I2S_SUPPORTS_PDM_TX
			if (parameters.direction == Direction::Tx) {
				return ResultStatus::notSupported;
			}
#endif
			return ResultStatus::ok;
		}
#if !SOC_I2S_SUPPORTS_TDM
		if (parameters.slotCount.Get() > 2) {
			return ResultStatus::notSupported;
		}
#endif
		// TDM has no right-justified format.
		if (parameters.slotCount.Get() > 2 && parameters.standard == Standard::LSB) {
			return ResultStatus::notSupported;
		}
#if SOC_I2S_HW_VERSION_1
		if (parameters.standard == Standard::LSB) {
			return ResultStatus::notSupported;
		}
#endif
		return ResultStatus::ok;
	}


	ResultStatus InitStdMode() {
		i2s_std_config_t cfg = {};

		i2s_std_clk_config_t clockConfig = I2S_STD_CLK_DEFAULT_CONFIG(parameters.sampleRateHz);
		clockConfig.clk_src = CastClockSource();
		clockConfig.mclk_multiple = (parameters.dataFormat == DataFormat::B24) ? I2S_MCLK_MULTIPLE_384 : I2S_MCLK_MULTIPLE_256;

		i2s_data_bit_width_t dataBitWidth = CastDataBitWidth();
		i2s_slot_bit_width_t slotBitWidth = CastSlotBitWidth();
		uint32 slotWidth = (slotBitWidth == I2S_SLOT_BIT_WIDTH_AUTO) ? static_cast<uint32>(dataBitWidth) : static_cast<uint32>(slotBitWidth);

		i2s_std_slot_config_t slotConfig = {};
		switch (parameters.standard) {
			case Standard::Philips:
				slotConfig = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(dataBitWidth, CastSlotMode());
				slotConfig.ws_width = slotWidth;
				break;
			case Standard::MSB:
				slotConfig = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(dataBitWidth, CastSlotMode());
				slotConfig.ws_width = slotWidth;
				break;
			case Standard::PCMShort:
				slotConfig = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(dataBitWidth, CastSlotMode());
				break;
			case Standard::PCMLong:
				slotConfig = I2S_STD_PCM_SLOT_DEFAULT_CONFIG(dataBitWidth, CastSlotMode());
				slotConfig.ws_width = slotWidth;
				break;
			case Standard::LSB:
#if SOC_I2S_HW_VERSION_1
				return ResultStatus::notSupported;
#else
				slotConfig = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(dataBitWidth, CastSlotMode());
				slotConfig.ws_width = slotWidth;
				slotConfig.left_align = false;
				break;
#endif
			case Standard::PDM:
				return ResultStatus::notSupported;
		}
		slotConfig.slot_bit_width = slotBitWidth;
		// Standard mode has two slots only; higher (TDM) bits are ignored and no bits at all keeps the default.
		uint32 standardSlots = parameters.activeSlots.Get() & (Slot::Left.Get() | Slot::Right.Get());
		if (standardSlots != 0) {
			slotConfig.slot_mask = static_cast<i2s_std_slot_mask_t>(standardSlots);
		}

		cfg.clk_cfg = clockConfig;
		cfg.slot_cfg = slotConfig;

		cfg.gpio_cfg.mclk = parameters.mclkOutput ? CastGpio(gpio.mclk) : I2S_GPIO_UNUSED;
		cfg.gpio_cfg.bclk = CastGpio(gpio.bclk);
		cfg.gpio_cfg.ws = CastGpio(gpio.ws);
		cfg.gpio_cfg.dout = CastGpio(gpio.dataOut);
		cfg.gpio_cfg.din = CastGpio(gpio.dataIn);
		cfg.gpio_cfg.invert_flags.mclk_inv = false;
		cfg.gpio_cfg.invert_flags.bclk_inv = (parameters.clockPolarity == ClockPolarity::High);
		cfg.gpio_cfg.invert_flags.ws_inv = false;

		if (txHandle) {
			if (i2s_channel_init_std_mode(txHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}
		if (rxHandle) {
			if (i2s_channel_init_std_mode(rxHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}

		return ResultStatus::ok;
	}


#if SOC_I2S_SUPPORTS_TDM
	ResultStatus InitTdmMode() {
		i2s_tdm_config_t cfg = {};

		cfg.clk_cfg.sample_rate_hz = parameters.sampleRateHz;
		cfg.clk_cfg.clk_src = CastClockSource();
		cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

		cfg.slot_cfg.data_bit_width = CastDataBitWidth();
		cfg.slot_cfg.slot_bit_width = CastSlotBitWidth();
		cfg.slot_cfg.slot_mode = CastSlotMode();
		cfg.slot_cfg.slot_mask = static_cast<i2s_tdm_slot_mask_t>(parameters.activeSlots.Get());
		cfg.slot_cfg.total_slot = parameters.slotCount.Get();

		switch (parameters.standard) {
			case Standard::Philips:
				cfg.slot_cfg.ws_width = cfg.slot_cfg.slot_bit_width;
				cfg.slot_cfg.ws_pol = false;
				cfg.slot_cfg.bit_shift = true;
				break;
			case Standard::MSB:
				cfg.slot_cfg.ws_width = cfg.slot_cfg.slot_bit_width;
				cfg.slot_cfg.ws_pol = false;
				cfg.slot_cfg.bit_shift = false;
				break;
			case Standard::PCMShort:
				cfg.slot_cfg.ws_width = 1;
				cfg.slot_cfg.ws_pol = true;
				cfg.slot_cfg.bit_shift = true;
				break;
			case Standard::PCMLong:
				cfg.slot_cfg.ws_width = cfg.slot_cfg.slot_bit_width;
				cfg.slot_cfg.ws_pol = true;
				cfg.slot_cfg.bit_shift = true;
				break;
			case Standard::LSB:
			case Standard::PDM:
				return ResultStatus::notSupported;
		}

		cfg.gpio_cfg.mclk = parameters.mclkOutput ? CastGpio(gpio.mclk) : I2S_GPIO_UNUSED;
		cfg.gpio_cfg.bclk = CastGpio(gpio.bclk);
		cfg.gpio_cfg.ws = CastGpio(gpio.ws);
		cfg.gpio_cfg.dout = CastGpio(gpio.dataOut);
		cfg.gpio_cfg.din = CastGpio(gpio.dataIn);
		cfg.gpio_cfg.invert_flags.mclk_inv = false;
		cfg.gpio_cfg.invert_flags.bclk_inv = (parameters.clockPolarity == ClockPolarity::High);
		cfg.gpio_cfg.invert_flags.ws_inv = false;

		if (txHandle) {
			if (i2s_channel_init_tdm_mode(txHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}
		if (rxHandle) {
			if (i2s_channel_init_tdm_mode(rxHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}

		return ResultStatus::ok;
	}
#endif


	ResultStatus InitPdmMode() {
		int clockPin = (gpio.bclk >= 0) ? gpio.bclk : gpio.ws;

#if SOC_I2S_SUPPORTS_PDM_RX
		if (rxHandle) {
			i2s_pdm_rx_clk_config_t clockConfig = I2S_PDM_RX_CLK_DEFAULT_CONFIG(parameters.sampleRateHz);
			clockConfig.clk_src = CastClockSource();
			clockConfig.dn_sample_mode = parameters.pdmDecimation == PdmDecimation::X128 ? I2S_PDM_DSR_16S : I2S_PDM_DSR_8S;

			i2s_pdm_rx_slot_config_t slotConfig = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, CastSlotMode());
			i2s_pdm_slot_mask_t slotMask = CastPdmSlotMask();
			if (slotMask != 0) {
				slotConfig.slot_mask = slotMask;
			}

			i2s_pdm_rx_gpio_config_t gpioConfig = {};
			gpioConfig.clk = CastGpio(clockPin);
			gpioConfig.din = CastGpio(gpio.dataIn);
			gpioConfig.invert_flags.clk_inv = (parameters.clockPolarity == ClockPolarity::High);

			i2s_pdm_rx_config_t cfg = {};
			cfg.clk_cfg = clockConfig;
			cfg.slot_cfg = slotConfig;
			cfg.gpio_cfg = gpioConfig;

			if (i2s_channel_init_pdm_rx_mode(rxHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}
#endif

#if SOC_I2S_SUPPORTS_PDM_TX
		if (txHandle) {
			i2s_pdm_tx_clk_config_t clockConfig = I2S_PDM_TX_CLK_DEFAULT_CONFIG(parameters.sampleRateHz);
			clockConfig.clk_src = CastClockSource();

			i2s_pdm_tx_slot_config_t slotConfig = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, CastSlotMode());
#if SOC_I2S_HW_VERSION_1
			i2s_pdm_slot_mask_t slotMask = CastPdmSlotMask();
			if (slotMask != 0) {
				slotConfig.slot_mask = slotMask;
			}
#endif

			i2s_pdm_tx_gpio_config_t gpioConfig = {};
			gpioConfig.clk = CastGpio(clockPin);
			gpioConfig.dout = CastGpio(gpio.dataOut);
			gpioConfig.invert_flags.clk_inv = (parameters.clockPolarity == ClockPolarity::High);

			i2s_pdm_tx_config_t cfg = {};
			cfg.clk_cfg = clockConfig;
			cfg.slot_cfg = slotConfig;
			cfg.gpio_cfg = gpioConfig;

			if (i2s_channel_init_pdm_tx_mode(txHandle, &cfg) != ESP_OK) {
				return ResultStatus::error;
			}
		}
#endif

		return ResultStatus::ok;
	}


	void Deinit() {
		if (txHandle) {
			i2s_channel_disable(txHandle);
			i2s_del_channel(txHandle);
			txHandle = nullptr;
			txEnabled = false;
		}
		if (rxHandle) {
			i2s_channel_disable(rxHandle);
			i2s_del_channel(rxHandle);
			rxHandle = nullptr;
			rxEnabled = false;
		}
	}
};
