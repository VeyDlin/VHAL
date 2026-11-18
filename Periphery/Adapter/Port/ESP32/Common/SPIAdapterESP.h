#pragma once
#include <Adapter/SPIAdapter.h>
#include <esp_memory_utils.h>


using ASPI = class SPIAdapterESP;


class SPIAdapterESP : public SPIAdapter<void> {
protected:
	static constexpr uint32 defaultMaxTransferSize = 4096;
	// Queue depth of the device, so the largest number of transfers one BeginWriteBytes can queue: 8 transfers of up to
	// 4096 bytes, which covers 32 KB (a 135 x 80 RGB565 band is 21600 bytes, six transfers).
	static constexpr uint32 maxBulkTransactions = 8;

	spi_host_device_t host = SPI2_HOST;
	spi_device_handle_t devHandle = nullptr;

	int mosiPin = -1;
	int misoPin = -1;
	int sclkPin = -1;
	int csPin = -1;

	uint32 maxTransferSize = defaultMaxTransferSize;
	uint32 chunkSize = defaultMaxTransferSize;
	bool busOwner = false;
	bool asyncPending = false;

	spi_transaction_t asyncTrans = {};

	// Transfers of BeginWriteBytes. They must stay in place until they are collected, so they are members.
	spi_transaction_t bulkTrans[maxBulkTransactions] = {};
	uint32 bulkInFlight = 0;
	bool bulkBusHeld = false;


public:
	SPIAdapterESP() = default;

	SPIAdapterESP(spi_host_device_t spiHost, int mosi, int miso, int sclk, int cs = -1, uint32 maxTransfer = defaultMaxTransferSize)
		: host(spiHost), mosiPin(mosi), misoPin(miso), sclkPin(sclk), csPin(cs), maxTransferSize(maxTransfer) { }

	~SPIAdapterESP() {
		Deinitialize();
	}


	void IrqHandler() override { }

	void AbortReceive() override {
		rxState = ResultStatus::ready;
	}

	void AbortTransmit() override {
		txState = ResultStatus::ready;
	}


	// Asynchronous write on the transaction queue. Every transfer is queued with the chip select kept active except the
	// last, which needs the bus acquired (ESP-IDF rejects SPI_TRANS_CS_KEEP_ACTIVE otherwise). Completion is polled, so no
	// callback runs in interrupt context.
	ResultStatus BeginWriteBytes(const uint8 *buffer, uint32 size) override {
		return QueueBulk(const_cast<uint8 *>(buffer), size);
	}


	ResultStatus WaitWriteBytes(uint32 timeoutMs) override {
		if (timeoutMs == UINT32_MAX) {
			return WaitBulk(portMAX_DELAY);
		}
		return WaitBulk(pdMS_TO_TICKS(timeoutMs));
	}


	// The classic ESP32 DMA reads internal RAM only (MALLOC_CAP_DMA excludes external PSRAM), so the buffer must lie in
	// internal memory from its first to its last byte.
	ResultStatus CheckDmaBuffer(const uint8 *buffer, uint32 size) override {
		if (buffer == nullptr || size == 0) {
			return ResultStatus::invalidParameter;
		}
		const uint8 *last = buffer + size - 1;
		if (!esp_ptr_dma_capable(buffer) || !esp_ptr_dma_capable(last)) {
			return ResultStatus::invalidParameter;
		}
		return ResultStatus::ok;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		if (parameters.mode == Mode::Slave) {
			return ResultStatus::notSupported;
		}

		Deinitialize();

		spi_bus_config_t bus_config = {};
		bus_config.mosi_io_num = mosiPin;
		bus_config.miso_io_num = misoPin;
		bus_config.sclk_io_num = sclkPin;
		bus_config.quadwp_io_num = -1;
		bus_config.quadhd_io_num = -1;
		bus_config.max_transfer_sz = static_cast<int>(maxTransferSize);

		esp_err_t busResult = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
		if (busResult == ESP_OK) {
			busOwner = true;
		} else if (busResult == ESP_ERR_INVALID_STATE) {
			busOwner = false;
		} else {
			return ResultStatus::error;
		}

		chunkSize = maxTransferSize;
		size_t maxBytes = 0;
		// A requested size of 0 means "driver default", so the bus limit is the only source of the chunk size then.
		if (spi_bus_get_max_transaction_len(host, &maxBytes) == ESP_OK && (chunkSize == 0 || maxBytes < chunkSize)) {
			chunkSize = static_cast<uint32>(maxBytes);
		}
		if (chunkSize == 0) {
			chunkSize = defaultMaxTransferSize;
		}

		spi_device_interface_config_t dev_config = {};
		dev_config.clock_speed_hz = parameters.maxSpeedHz;
		dev_config.mode = CastSpiMode();
		dev_config.spics_io_num = csPin;
		dev_config.queue_size = maxBulkTransactions;
		dev_config.flags = (parameters.firstBit == FirstBit::LSB) ? SPI_DEVICE_BIT_LSBFIRST : 0;
		// A transmit-only device reads nothing back, so the driver must not cap the clock at what the read path
		// through the GPIO matrix can sample (26.6 MHz on pins other than the native ones).
		if (parameters.direction == Direction::Tx) {
			dev_config.flags |= SPI_DEVICE_NO_DUMMY;
		}
		dev_config.post_cb = PostTransactionCallback;

		if (spi_bus_add_device(host, &dev_config, &devHandle) != ESP_OK) {
			// The bus is deliberately left allocated: on some failures (an unsupported clock, for one) ESP-IDF keeps
			// the device's bus-lock slot registered, and spi_bus_free() then aborts on an assertion.
			devHandle = nullptr;
			busOwner = false;
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


	uint32 CalculatePrescaler() override {
		return 0;
	}


	ResultStatus WriteByteArray(uint8 *buffer, uint32 size) override {
		return Transfer(buffer, nullptr, size);
	}


	ResultStatus ReadByteArray(uint8 *buffer, uint32 size) override {
		return Transfer(nullptr, buffer, size);
	}


	ResultStatus WriteReadByteArray(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		return Transfer(txBuffer, rxBuffer, size);
	}


	ResultStatus WriteByteArrayAsync(uint8 *buffer, uint32 size) override {
		return QueueAsync(buffer, nullptr, size);
	}


	ResultStatus ReadByteArrayAsync(uint8 *buffer, uint32 size) override {
		return QueueAsync(nullptr, buffer, size);
	}


	ResultStatus WriteReadByteArrayAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) override {
		return QueueAsync(txBuffer, rxBuffer, size);
	}


private:
	uint8 CastSpiMode() const {
		uint8 cpol = (parameters.clockPolarity == ClockPolarity::High) ? 1 : 0;
		uint8 cpha = (parameters.clockPhase == ClockPhase::Edge2) ? 1 : 0;
		return (cpol << 1) | cpha;
	}

	// Deliberately not IRAM_ATTR: the bus is initialised without ESP_INTR_FLAG_IRAM, so the driver ISR, and with it this
	// callback, never runs while the flash cache is disabled. An in-class IRAM_ATTR function fails to link at -O2
	// ("dangerous relocation: l32r"), as in GPIOAdapterESP.h.
	static void PostTransactionCallback(spi_transaction_t *trans) {
		auto *self = static_cast<SPIAdapterESP*>(trans->user);
		if (!self) {
			return;
		}

		if (trans->tx_buffer) {
			self->txState = ResultStatus::ready;
			self->CallInterrupt(Irq::Tx);
		}
		if (trans->rx_buffer) {
			self->rxState = ResultStatus::ready;
			self->CallInterrupt(Irq::Rx);
		}
	}


	void Deinitialize() {
		if (bulkInFlight > 0 || bulkBusHeld) {
			// Same reasoning as below: the transfers finish on their own, and the device cannot be removed before they do.
			WaitBulk(portMAX_DELAY);
		}

		if (asyncPending) {
			// Waits without a limit: the transfer is clocked by this master, so it always finishes, and the driver
			// refuses to remove a device that still has one in flight.
			spi_transaction_t *finished = nullptr;
			spi_device_get_trans_result(devHandle, &finished, portMAX_DELAY);
			asyncPending = false;
		}

		if (devHandle) {
			spi_bus_remove_device(devHandle);
			devHandle = nullptr;
		}

		if (busOwner) {
			spi_bus_free(host);
		}
		busOwner = false;
	}


	ResultStatus WaitAsync() {
		if (!asyncPending) {
			return ResultStatus::ok;
		}

		spi_transaction_t *finished = nullptr;
		if (spi_device_get_trans_result(devHandle, &finished, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			return ResultStatus::timeout;
		}
		asyncPending = false;
		return ResultStatus::ok;
	}


	// Queues the transfers of one write. A single transfer needs no bus acquisition; several need it for the chip select
	// to stay low between them. The bus is released by WaitBulk once every transfer has been collected.
	ResultStatus QueueBulk(uint8 *buffer, uint32 size) {
		if (devHandle == nullptr) {
			return ResultStatus::noInit;
		}
		if (size == 0) {
			return ResultStatus::ok;
		}
		if (bulkInFlight > 0) {
			return ResultStatus::busy;
		}

		uint32 pieces = (size + chunkSize - 1) / chunkSize;
		if (pieces > maxBulkTransactions) {
			return ResultStatus::outOfRange;
		}

		ResultStatus asyncStatus = WaitAsync();
		if (asyncStatus != ResultStatus::ok) {
			return asyncStatus;
		}

		bool holdBus = pieces > 1;
		if (holdBus) {
			if (spi_device_acquire_bus(devHandle, portMAX_DELAY) != ESP_OK) {
				return ResultStatus::error;
			}
			bulkBusHeld = true;
		}

		uint32 remaining = size;
		uint32 queued = 0;
		while (remaining > 0) {
			uint32 bytes = (remaining > chunkSize) ? chunkSize : remaining;
			bool isLast = (bytes == remaining);

			spi_transaction_t *trans = &bulkTrans[queued];
			*trans = {};
			trans->length = bytes * 8;
			trans->tx_buffer = buffer;
			if (holdBus && !isLast) {
				trans->flags = SPI_TRANS_CS_KEEP_ACTIVE;
			}

			if (spi_device_queue_trans(devHandle, trans, pdMS_TO_TICKS(timeout)) != ESP_OK) {
				// Collect what was queued, so the buffer is no longer in use and the bus is released.
				bulkInFlight = queued;
				WaitBulk(portMAX_DELAY);
				return ResultStatus::error;
			}

			queued++;
			bulkInFlight = queued;
			buffer += bytes;
			remaining -= bytes;
		}
		return ResultStatus::ok;
	}


	// Collects the queued transfers of BeginWriteBytes. Each ticks argument is the time allowed per transfer.
	ResultStatus WaitBulk(TickType_t ticks) {
		while (bulkInFlight > 0) {
			spi_transaction_t *finished = nullptr;
			if (spi_device_get_trans_result(devHandle, &finished, ticks) != ESP_OK) {
				return ResultStatus::timeout;
			}
			bulkInFlight--;
		}

		if (bulkBusHeld) {
			spi_device_release_bus(devHandle);
			bulkBusHeld = false;
		}
		return ResultStatus::ok;
	}


	ResultStatus Transfer(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) {
		if (devHandle == nullptr) {
			return ResultStatus::noInit;
		}
		if (size == 0) {
			return ResultStatus::ok;
		}

		// A synchronous transfer must not interleave with a BeginWriteBytes that is still running.
		if (bulkInFlight > 0 || bulkBusHeld) {
			ResultStatus bulkStatus = WaitBulk(pdMS_TO_TICKS(timeout));
			if (bulkStatus != ResultStatus::ok) {
				return bulkStatus;
			}
		}

		ResultStatus asyncStatus = WaitAsync();
		if (asyncStatus != ResultStatus::ok) {
			return asyncStatus;
		}

		bool holdBus = (csPin >= 0) && (size > chunkSize);
		if (holdBus && spi_device_acquire_bus(devHandle, portMAX_DELAY) != ESP_OK) {
			return ResultStatus::error;
		}

		ResultStatus status = ResultStatus::ok;
		uint32 remaining = size;
		while (remaining > 0) {
			uint32 bytes = (remaining > chunkSize) ? chunkSize : remaining;
			bool isLast = (bytes == remaining);

			spi_transaction_t trans = {};
			trans.length = bytes * 8;
			trans.tx_buffer = txBuffer;
			trans.rx_buffer = rxBuffer;
			if (rxBuffer) {
				trans.rxlength = bytes * 8;
			}
			if (holdBus && !isLast) {
				trans.flags = SPI_TRANS_CS_KEEP_ACTIVE;
			}

			if (spi_device_transmit(devHandle, &trans) != ESP_OK) {
				status = ResultStatus::error;
				break;
			}

			if (txBuffer) {
				txBuffer += bytes;
			}
			if (rxBuffer) {
				rxBuffer += bytes;
			}
			remaining -= bytes;
		}

		if (holdBus) {
			spi_device_release_bus(devHandle);
		}
		return status;
	}


	ResultStatus QueueAsync(uint8 *txBuffer, uint8 *rxBuffer, uint32 size) {
		if (devHandle == nullptr) {
			return ResultStatus::noInit;
		}
		if (size > chunkSize) {
			return ResultStatus::outOfRange;
		}

		if (asyncPending) {
			spi_transaction_t *finished = nullptr;
			if (spi_device_get_trans_result(devHandle, &finished, 0) != ESP_OK) {
				return ResultStatus::busy;
			}
			asyncPending = false;
		}

		asyncTrans = {};
		asyncTrans.length = size * 8;
		asyncTrans.tx_buffer = txBuffer;
		asyncTrans.rx_buffer = rxBuffer;
		if (rxBuffer) {
			asyncTrans.rxlength = size * 8;
		}
		asyncTrans.user = this;

		if (txBuffer) {
			txState = ResultStatus::busy;
		}
		if (rxBuffer) {
			rxState = ResultStatus::busy;
		}

		if (spi_device_queue_trans(devHandle, &asyncTrans, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			if (txBuffer) {
				txState = ResultStatus::error;
			}
			if (rxBuffer) {
				rxState = ResultStatus::error;
			}
			return ResultStatus::error;
		}

		asyncPending = true;
		return ResultStatus::ok;
	}
};
