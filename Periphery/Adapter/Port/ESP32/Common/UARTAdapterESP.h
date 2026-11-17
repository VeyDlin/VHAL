#pragma once
#include <Adapter/UARTAdapter.h>


using AUART = class UARTAdapterESP;


class UARTAdapterESP : public UARTAdapter<void> {
public:
	static constexpr uint32 defaultBufferSize = 1024;
	static constexpr uint32 eventQueueSize = 16;


protected:
	uart_port_t port = UART_NUM_0;
	int txPin = UART_PIN_NO_CHANGE;
	int rxPin = UART_PIN_NO_CHANGE;
	int rtsPin = UART_PIN_NO_CHANGE;
	int ctsPin = UART_PIN_NO_CHANGE;
	uint32 bufferSize = defaultBufferSize;
	bool driverInstalled = false;

	QueueHandle_t eventQueue = nullptr;
	TaskHandle_t rxTaskHandle = nullptr;


private:
	static constexpr uint32 rxTaskStackSize = 4096;
	static constexpr uint32 rxTaskPriority = 10;


public:
	UARTAdapterESP() = default;

	UARTAdapterESP(uart_port_t uartPort, int tx, int rx, int rts = UART_PIN_NO_CHANGE, int cts = UART_PIN_NO_CHANGE, uint32 bufSize = defaultBufferSize)
		: port(uartPort), txPin(tx), rxPin(rx), rtsPin(rts), ctsPin(cts), bufferSize(bufSize) { }

	~UARTAdapterESP() {
		StopRxTask();
		if (driverInstalled) {
			uart_driver_delete(port);
			driverInstalled = false;
		}
	}


	void IrqHandler() override { }

	void AbortReceive() override {
		uart_flush_input(port);
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;
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

		StopRxTask();
		if (driverInstalled) {
			uart_driver_delete(port);
			driverInstalled = false;
		}

		// The receive task is gone, so an unfinished read or continuous mode cannot complete: start clean.
		rxState = ResultStatus::ready;
		rxDataPointer = nullptr;
		rxDataNeed = 0;
		rxDataCounter = 0;
		continuousAsyncRxMode = false;

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
		driverInstalled = true;

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
		if (continuousAsyncRxMode) {
			return ResultStatus::notAvailable;
		}

		if (rxState == ResultStatus::busy) {
			return ResultStatus::busy;
		}

		if (!driverInstalled) {
			return ResultStatus::noInit;
		}

		rxDataPointer = buffer;
		rxDataNeed = size;
		rxDataCounter = 0;
		rxState = ResultStatus::busy;

		ResultStatus taskStatus = EnsureRxTask();
		if (taskStatus != ResultStatus::ok) {
			rxState = ResultStatus::ready;
			return taskStatus;
		}

		uart_event_t event = {};
		event.type = UART_DATA;
		xQueueSend(eventQueue, &event, 0);

		return ResultStatus::ok;
	}


	ResultStatus StartContinuousAsyncRxMode() override {
		if (rxState == ResultStatus::busy) {
			return ResultStatus::busy;
		}

		if (!driverInstalled) {
			return ResultStatus::noInit;
		}

		return EnsureRxTask();
	}


	ResultStatus StopContinuousAsyncRxMode() override {
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

	ResultStatus EnsureRxTask() {
		if (rxTaskHandle) {
			return ResultStatus::ok;
		}

		BaseType_t created = xTaskCreate(
			RxEventTask,
			"uart_rx",
			rxTaskStackSize,
			this,
			rxTaskPriority,
			&rxTaskHandle
		);

		return (created == pdTRUE) ? ResultStatus::ok : ResultStatus::error;
	}


	void StopRxTask() {
		if (rxTaskHandle) {
			vTaskDelete(rxTaskHandle);
			rxTaskHandle = nullptr;
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
					if (self->continuousAsyncRxMode) {
						uint8 byte = 0;
						while (uart_read_bytes(self->port, &byte, 1, 0) >= 1) {
							self->lastRxData = byte;
							self->CallInterrupt(Irq::Rx);
						}
					} else if (self->rxState == ResultStatus::busy && self->rxDataPointer) {
						uint32 remaining = static_cast<uint32>(self->rxDataNeed - self->rxDataCounter);
						int read = uart_read_bytes(self->port, self->rxDataPointer + self->rxDataCounter, remaining, 0);
						if (read > 0) {
							self->rxDataCounter = static_cast<uint16>(self->rxDataCounter + read);
						}
						if (self->rxDataCounter >= self->rxDataNeed) {
							self->rxState = ResultStatus::ready;
							self->CallInterrupt(Irq::Rx);
						}
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
