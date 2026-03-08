#pragma once
#include <Adapter/I2SAdapter.h>


using AI2S = class I2SAdapterESP;


class I2SAdapterESP : public I2SAdapter<void> {
public:
	struct ClockSource {
		static inline constexpr ClockSourceOption Default { I2S_CLK_SRC_DEFAULT };
	};

	struct SlotCount {
		static inline constexpr SlotCountOption S1  { 1 };
		static inline constexpr SlotCountOption S2  { 2 };
		static inline constexpr SlotCountOption S4  { 4 };
		static inline constexpr SlotCountOption S8  { 8 };
		static inline constexpr SlotCountOption S16 { 16 };
	};

	struct Slot {
		static inline constexpr SlotOption S0  { I2S_TDM_SLOT0 };
		static inline constexpr SlotOption S1  { I2S_TDM_SLOT1 };
		static inline constexpr SlotOption S2  { I2S_TDM_SLOT2 };
		static inline constexpr SlotOption S3  { I2S_TDM_SLOT3 };
		static inline constexpr SlotOption S4  { I2S_TDM_SLOT4 };
		static inline constexpr SlotOption S5  { I2S_TDM_SLOT5 };
		static inline constexpr SlotOption S6  { I2S_TDM_SLOT6 };
		static inline constexpr SlotOption S7  { I2S_TDM_SLOT7 };
		static inline constexpr SlotOption S8  { I2S_TDM_SLOT8 };
		static inline constexpr SlotOption S9  { I2S_TDM_SLOT9 };
		static inline constexpr SlotOption S10 { I2S_TDM_SLOT10 };
		static inline constexpr SlotOption S11 { I2S_TDM_SLOT11 };
		static inline constexpr SlotOption S12 { I2S_TDM_SLOT12 };
		static inline constexpr SlotOption S13 { I2S_TDM_SLOT13 };
		static inline constexpr SlotOption S14 { I2S_TDM_SLOT14 };
		static inline constexpr SlotOption S15 { I2S_TDM_SLOT15 };

		static inline constexpr SlotOption Mono   { I2S_TDM_SLOT0 };
		static inline constexpr SlotOption Stereo { I2S_TDM_SLOT0 | I2S_TDM_SLOT1 };
	};

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

	I2SAdapterESP(i2s_port_t port, GpioConfig gpio, DmaConfig dma = {})
		: port(port), gpio(gpio), dma(dma) { }

	~I2SAdapterESP() {
		Deinit();
	}


	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}


	ResultStatus Transmit(uint16 *buffer, uint32 size) override {
		if (!txHandle) {
			return ResultStatus::error;
		}
		size_t bytesWritten = 0;
		if (i2s_channel_write(txHandle, buffer, size * sizeof(uint16), &bytesWritten, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
	}


	ResultStatus Receive(uint16 *buffer, uint32 size) override {
		if (!rxHandle) {
			return ResultStatus::error;
		}
		size_t bytesRead = 0;
		if (i2s_channel_read(rxHandle, buffer, size * sizeof(uint16), &bytesRead, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			return ResultStatus::error;
		}
		return ResultStatus::ok;
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
			return ResultStatus::error;
		}
		return (i2s_channel_disable(txHandle) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}

	ResultStatus StopReceive() override {
		if (!rxHandle) {
			return ResultStatus::error;
		}
		return (i2s_channel_disable(rxHandle) == ESP_OK) ? ResultStatus::ok : ResultStatus::error;
	}


	void IrqHandler() override { }


protected:
	i2s_port_t port = I2S_NUM_AUTO;
	GpioConfig gpio;
	DmaConfig dma;
	i2s_chan_handle_t txHandle = nullptr;
	i2s_chan_handle_t rxHandle = nullptr;


	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
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

		if (parameters.slotCount.Get() > 2) {
			status = InitTdmMode();
		} else {
			status = InitStdMode();
		}

		if (status != ResultStatus::ok) {
			Deinit();
			return status;
		}

		if (txHandle && i2s_channel_enable(txHandle) != ESP_OK) {
			Deinit();
			return ResultStatus::error;
		}

		if (rxHandle && i2s_channel_enable(rxHandle) != ESP_OK) {
			Deinit();
			return ResultStatus::error;
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


	ResultStatus InitStdMode() {
		i2s_std_config_t cfg = {};

		cfg.clk_cfg.sample_rate_hz = parameters.sampleRateHz;
		cfg.clk_cfg.clk_src = static_cast<i2s_clock_src_t>(parameters.clockSource.Get());
		cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

		cfg.slot_cfg.data_bit_width = CastDataBitWidth();
		cfg.slot_cfg.slot_bit_width = CastSlotBitWidth();
		cfg.slot_cfg.slot_mode = CastSlotMode();

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
				cfg.slot_cfg.ws_width = cfg.slot_cfg.slot_bit_width;
				cfg.slot_cfg.ws_pol = false;
				cfg.slot_cfg.bit_shift = false;
				cfg.slot_cfg.left_align = false;
				break;
		}

		cfg.gpio_cfg.mclk = static_cast<gpio_num_t>(gpio.mclk);
		cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(gpio.bclk);
		cfg.gpio_cfg.ws = static_cast<gpio_num_t>(gpio.ws);
		cfg.gpio_cfg.dout = static_cast<gpio_num_t>(gpio.dataOut);
		cfg.gpio_cfg.din = static_cast<gpio_num_t>(gpio.dataIn);
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


	ResultStatus InitTdmMode() {
		i2s_tdm_config_t cfg = {};

		cfg.clk_cfg.sample_rate_hz = parameters.sampleRateHz;
		cfg.clk_cfg.clk_src = static_cast<i2s_clock_src_t>(parameters.clockSource.Get());
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
				return ResultStatus::notSupported;
		}

		cfg.gpio_cfg.mclk = static_cast<gpio_num_t>(gpio.mclk);
		cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(gpio.bclk);
		cfg.gpio_cfg.ws = static_cast<gpio_num_t>(gpio.ws);
		cfg.gpio_cfg.dout = static_cast<gpio_num_t>(gpio.dataOut);
		cfg.gpio_cfg.din = static_cast<gpio_num_t>(gpio.dataIn);
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


	void Deinit() {
		if (txHandle) {
			i2s_channel_disable(txHandle);
			i2s_del_channel(txHandle);
			txHandle = nullptr;
		}
		if (rxHandle) {
			i2s_channel_disable(rxHandle);
			i2s_del_channel(rxHandle);
			rxHandle = nullptr;
		}
	}
};
