#pragma once
#include <Adapter/UARTAdapter.h>


using AUART = class UARTAdapterESP;


class UARTAdapterESP : public UARTAdapter<void> {
public:
	static constexpr uint32 defaultBufferSize = 1024;
	static constexpr uint32 eventQueueSize = 16;


protected:
	uart_port_t port;
	int txPin;
	int rxPin;
	int rtsPin;
	int ctsPin;
	uint32 bufferSize;

	QueueHandle_t eventQueue = nullptr;
	TaskHandle_t rxTaskHandle = nullptr;


public:
	UARTAdapterESP() = default;

	UARTAdapterESP(uart_port_t uartPort, int tx, int rx, int rts = UART_PIN_NO_CHANGE, int cts = UART_PIN_NO_CHANGE, uint32 bufSize = defaultBufferSize)
		: port(uartPort), txPin(tx), rxPin(rx), rtsPin(rts), ctsPin(cts), bufferSize(bufSize) { }

	~UARTAdapterESP() {
		StopContinuousAsyncRxMode();
		uart_driver_delete(port);
	}


	void IrqHandler() override { }

	void AbortReceive() override {
		uart_flush_input(port);
		rxState = ResultStatus::ready;
	}

	void AbortTransmit() override {
		uart_wait_tx_done(port, 0);
		txState = ResultStatus::ready;
	}


protected:
	ResultStatus Initialization() override {
		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		uart_driver_delete(port);

		uart_config_t uart_config = {};
		uart_config.baud_rate = parameters.baudRate;
		uart_config.data_bits = UART_DATA_8_BITS;
		uart_config.parity = CastParity();
		uart_config.stop_bits = CastStopBits();
		uart_config.flow_ctrl = CastFlowControl();
		uart_config.source_clk = UART_SCLK_DEFAULT;

		if (uart_param_config(port, &uart_config) != ESP_OK) {
			return ResultStatus::error;
		}

		if (uart_set_pin(port, txPin, rxPin, rtsPin, ctsPin) != ESP_OK) {
			return ResultStatus::error;
		}

		if (uart_driver_install(port, bufferSize, bufferSize, eventQueueSize, &eventQueue, 0) != ESP_OK) {
			return ResultStatus::error;
		}

		return AfterInitialization();
	}


	ResultStatus WriteByteArray(uint8* buffer, uint32 size) override {
		int written = uart_write_bytes(port, buffer, size);
		if (written < 0) {
			return ResultStatus::error;
		}
		if (uart_wait_tx_done(port, pdMS_TO_TICKS(timeout)) != ESP_OK) {
			return ResultStatus::timeout;
		}
		return ResultStatus::ok;
	}


	ResultStatus ReadByteArray(uint8* buffer, uint32 size) override {
		int read = uart_read_bytes(port, buffer, size, pdMS_TO_TICKS(timeout));
		if (read < 0) {
			return ResultStatus::error;
		}
		if (static_cast<uint32>(read) < size) {
			return ResultStatus::timeout;
		}
		return ResultStatus::ok;
	}


	ResultStatus WriteByteArrayAsync(uint8* buffer, uint32 size) override {
		txState = ResultStatus::busy;
		txDataPointer = buffer;
		txDataNeed = size;
		txDataCounter = 0;

		int written = uart_write_bytes(port, buffer, size);
		if (written < 0) {
			txState = ResultStatus::error;
			return ResultStatus::error;
		}

		txDataCounter = written;
		txState = ResultStatus::ready;
		CallInterrupt(Irq::Tx);
		return ResultStatus::ok;
	}


	ResultStatus ReadByteArrayAsync(uint8* buffer, uint32 size) override {
		rxState = ResultStatus::busy;
		rxDataPointer = buffer;
		rxDataNeed = size;
		rxDataCounter = 0;

		int read = uart_read_bytes(port, buffer, size, 0);
		if (read < 0) {
			rxState = ResultStatus::error;
			return ResultStatus::error;
		}

		if (static_cast<uint32>(read) >= size) {
			rxDataCounter = read;
			rxState = ResultStatus::ready;
			CallInterrupt(Irq::Rx);
		}

		return ResultStatus::ok;
	}


	ResultStatus StartContinuousAsyncRxMode() override {
		if (rxTaskHandle) {
			return ResultStatus::ok;
		}

		auto created = xTaskCreate(
			RxEventTask,
			"uart_rx",
			2048,
			this,
			10,
			&rxTaskHandle
		);

		return (created == pdTRUE) ? ResultStatus::ok : ResultStatus::error;
	}


	ResultStatus StopContinuousAsyncRxMode() override {
		if (rxTaskHandle) {
			vTaskDelete(rxTaskHandle);
			rxTaskHandle = nullptr;
		}
		return ResultStatus::ok;
	}


private:
	uart_parity_t CastParity() const {
		switch (parameters.parity) {
			case Parity::Even: return UART_PARITY_EVEN;
			case Parity::Odd:  return UART_PARITY_ODD;
			default:           return UART_PARITY_DISABLE;
		}
	}

	uart_stop_bits_t CastStopBits() const {
		switch (parameters.stopBits) {
			case StopBits::B2: return UART_STOP_BITS_2;
			default:           return UART_STOP_BITS_1;
		}
	}

	uart_hw_flowcontrol_t CastFlowControl() const {
		switch (parameters.flowControl) {
			case FlowControl::Rts:    return UART_HW_FLOWCTRL_RTS;
			case FlowControl::Cts:    return UART_HW_FLOWCTRL_CTS;
			case FlowControl::RtsCts: return UART_HW_FLOWCTRL_CTS_RTS;
			default:                  return UART_HW_FLOWCTRL_DISABLE;
		}
	}

	static void RxEventTask(void *arg) {
		auto *self = static_cast<UARTAdapterESP*>(arg);
		uart_event_t event;

		while (true) {
			if (xQueueReceive(self->eventQueue, &event, portMAX_DELAY) != pdTRUE) {
				continue;
			}

			switch (event.type) {
				case UART_DATA:
					self->lastRxData = 0;
					if (self->rxDataPointer && self->rxDataNeed > 0) {
						int read = uart_read_bytes(self->port, self->rxDataPointer + self->rxDataCounter,
							self->rxDataNeed - self->rxDataCounter, 0);
						if (read > 0) {
							self->rxDataCounter += read;
						}
						if (self->rxDataCounter >= self->rxDataNeed) {
							self->rxState = ResultStatus::ready;
							self->CallInterrupt(Irq::Rx);
						}
					} else {
						self->CallInterrupt(Irq::Rx);
					}
				break;

				case UART_FIFO_OVF:
				case UART_BUFFER_FULL:
					uart_flush_input(self->port);
					xQueueReset(self->eventQueue);
					self->CallError(Error::Overrun);
				break;

				case UART_FRAME_ERR:
					self->CallError(Error::Frame);
				break;

				case UART_PARITY_ERR:
					self->CallError(Error::Parity);
				break;

				default:
				break;
			}
		}
	}
};
