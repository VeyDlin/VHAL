#pragma once
#include <Adapter/SPIAdapter.h>


using ASPI = class SPIAdapterESP;


class SPIAdapterESP : public SPIAdapter<void> {
protected:
	spi_host_device_t host;
	spi_device_handle_t devHandle = nullptr;

	int mosiPin;
	int misoPin;
	int sclkPin;
	int csPin;

	spi_transaction_t asyncTrans = {};


public:
	SPIAdapterESP() = default;

	SPIAdapterESP(spi_host_device_t spiHost, int mosi, int miso, int sclk, int cs = -1)
		: host(spiHost), mosiPin(mosi), misoPin(miso), sclkPin(sclk), csPin(cs) { }

	~SPIAdapterESP() {
		if (devHandle) {
			spi_bus_remove_device(devHandle);
		}
		spi_bus_free(host);
	}


	void IrqHandler() override { }

	void AbortReceive() override {
		rxState = Status::ready;
	}

	void AbortTransmit() override {
		txState = Status::ready;
	}


protected:
	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		if (devHandle) {
			spi_bus_remove_device(devHandle);
			devHandle = nullptr;
			spi_bus_free(host);
		}

		spi_bus_config_t bus_config = {};
		bus_config.mosi_io_num = mosiPin;
		bus_config.miso_io_num = misoPin;
		bus_config.sclk_io_num = sclkPin;
		bus_config.quadwp_io_num = -1;
		bus_config.quadhd_io_num = -1;
		bus_config.max_transfer_sz = 4096;

		if (spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO) != ESP_OK) {
			return Status::error;
		}

		spi_device_interface_config_t dev_config = {};
		dev_config.clock_speed_hz = parameters.maxSpeedHz;
		dev_config.mode = CastSpiMode();
		dev_config.spics_io_num = csPin;
		dev_config.queue_size = 4;
		dev_config.flags = (parameters.firstBit == FirstBit::LSB) ? SPI_DEVICE_BIT_LSBFIRST : 0;
		dev_config.post_cb = PostTransactionCallback;

		if (spi_bus_add_device(host, &dev_config, &devHandle) != ESP_OK) {
			return Status::error;
		}

		return AfterInitialization();
	}


	uint32 CalculatePrescaler() override {
		return 0;
	}


	Status::statusType WriteByteArray(uint8 *buffer, uint32 size) override {
		spi_transaction_t trans = {};
		trans.length = size * 8;
		trans.tx_buffer = buffer;

		if (spi_device_transmit(devHandle, &trans) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType ReadByteArray(uint8 *buffer, uint32 size) override {
		spi_transaction_t trans = {};
		trans.rxlength = size * 8;
		trans.rx_buffer = buffer;
		trans.length = size * 8;

		if (spi_device_transmit(devHandle, &trans) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		spi_transaction_t trans = {};
		trans.length = size * 8;
		trans.tx_buffer = txBuffer;
		trans.rxlength = size * 8;
		trans.rx_buffer = rxBuffer;

		if (spi_device_transmit(devHandle, &trans) != ESP_OK) {
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType WriteByteArrayAsync(uint8 *buffer, uint32 size) override {
		asyncTrans = {};
		asyncTrans.length = size * 8;
		asyncTrans.tx_buffer = buffer;
		asyncTrans.user = this;

		txState = Status::busy;
		if (spi_device_queue_trans(devHandle, &asyncTrans, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			txState = Status::error;
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		asyncTrans = {};
		asyncTrans.length = size * 8;
		asyncTrans.rxlength = size * 8;
		asyncTrans.rx_buffer = buffer;
		asyncTrans.user = this;

		rxState = Status::busy;
		if (spi_device_queue_trans(devHandle, &asyncTrans, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			rxState = Status::error;
			return Status::error;
		}
		return Status::ok;
	}


	Status::statusType WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		asyncTrans = {};
		asyncTrans.length = size * 8;
		asyncTrans.tx_buffer = txBuffer;
		asyncTrans.rxlength = size * 8;
		asyncTrans.rx_buffer = rxBuffer;
		asyncTrans.user = this;

		txState = Status::busy;
		rxState = Status::busy;
		if (spi_device_queue_trans(devHandle, &asyncTrans, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			txState = Status::error;
			rxState = Status::error;
			return Status::error;
		}
		return Status::ok;
	}


private:
	uint8 CastSpiMode() const {
		uint8 cpol = (parameters.clockPolarity == ClockPolarity::High) ? 1 : 0;
		uint8 cpha = (parameters.clockPhase == ClockPhase::Edge2) ? 1 : 0;
		return (cpol << 1) | cpha;
	}

	static void IRAM_ATTR PostTransactionCallback(spi_transaction_t *trans) {
		auto *self = static_cast<SPIAdapterESP*>(trans->user);
		if (!self) {
			return;
		}

		if (trans->tx_buffer) {
			self->txState = Status::ready;
			self->CallInterrupt(Irq::Tx);
		}
		if (trans->rx_buffer) {
			self->rxState = Status::ready;
			self->CallInterrupt(Irq::Rx);
		}
	}
};
