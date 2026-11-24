#pragma once
#include "IAdapter.h"

#define VHAL_UART_ADAPTER


template<typename HandleType>
class UARTAdapter: public IAdapter {
public:
	enum class DataBits { B7, B8 };
	enum class StopBits { B1, B2 };
	enum class Parity { None, Even, Odd };
	enum class Mode { Tx, Rx, TxRx };
	enum class FlowControl { None, Rts, Cts, RtsCts };
	enum class OverSampling { B8, B16 };

	enum class Irq { Tx, Rx };

	enum class Error { None, Parity, Noise, Frame, Overrun };

	struct Parameters {
		uint32 baudRate = 115200;
		StopBits stopBits = StopBits::B1;
		Parity parity = Parity::None;
		Mode mode = Mode::TxRx;
		FlowControl flowControl = FlowControl::None;
		OverSampling overSampling = OverSampling::B16;
		// The parity bit, when parity is on, is added to these data bits on every port.
		DataBits dataBits = DataBits::B8;
	};


protected:
	HandleType *uartHandle;
	Parameters parameters;

	uint32 timeout = 1000;

	bool continuousAsyncRxMode = false;
	ResultStatus rxState = ResultStatus::ready;
	uint32 rxDataNeed = 0;
	uint32 rxDataCounter = 0;
	uint8 *rxDataPointer = nullptr;
	uint8 lastRxData = 0;

	bool continuousAsyncTxMode = false;
	ResultStatus txState = ResultStatus::ready;
	uint32 txDataNeed = 0;
	uint32 txDataCounter = 0;
	uint8 *txDataPointer = nullptr;


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;





public:
	UARTAdapter() = default;
	UARTAdapter(HandleType *uart): uartHandle(uart) { }


	// Replaces onInterrupt while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnInterrupt(std::function<void(Irq irqType)> handler) {
		std::function<void(Irq irqType)> old;
		System::CriticalSection(true);
		old = std::move(onInterrupt);
		onInterrupt = std::move(handler);
		System::CriticalSection(false);
	}


	// Replaces onError while its interrupt may run. See "Changing a callback while its interrupt may run" in the wiki.
	void SetOnError(std::function<void(Error errorType)> handler) {
		std::function<void(Error errorType)> old;
		System::CriticalSection(true);
		old = std::move(onError);
		onError = std::move(handler);
		System::CriticalSection(false);
	}





	template <typename DataType>
	inline ResultStatus Write(DataType data) {
		return WriteByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}


	template <typename DataType>
	inline ResultStatus WriteArray(DataType* buffer, uint32 size) {
		return WriteByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType>
	inline ResultStatus WriteArray(const DataType* buffer, uint32 size) {
		return WriteByteArray(const_cast<uint8*>(reinterpret_cast<const uint8*>(buffer)), sizeof(DataType) * size);
	}


	ResultStatus WriteString(char *string) {
		size_t size = 0;
		while(string[size]) {
			size++;
		}

		return WriteByteArray(reinterpret_cast<uint8*>(string), size);
	}


	inline ResultStatus WriteString(const char* string) {
		return WriteString(const_cast<char*>(string));
	}


	template <typename DataType>
	ResultStatus ReadArray(DataType* buffer, uint32 size = 1) {
		return ReadByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	// Reads one value of DataType. For several values use ReadArray().
	template <typename DataType>
	inline Result<DataType> Read() {
		DataType data;
		return Result<DataType>::Capture(
			ReadByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType)), data
		);
	}





	// ---------------





	template <typename DataType>
	inline ResultStatus WriteAsync(DataType &data) {
		return StartWriteAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





	template <typename DataType>
	ResultStatus WriteArrayAsync(DataType* buffer, uint32 size) {
		return StartWriteAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline ResultStatus WriteArrayAsync(const DataType* buffer, uint32 size) {
		return StartWriteAsync(const_cast<uint8*>(reinterpret_cast<const uint8*>(buffer)), sizeof(DataType) * size);
	}





	inline ResultStatus WriteStringAsync(char *string) {
		size_t size = 0;
		while(string[size]) {
			size++;
		}

		return StartWriteAsync(reinterpret_cast<uint8*>(string), static_cast<uint32>(size));
	}





	inline ResultStatus WriteStringAsync(const char* string) {
		return WriteStringAsync(const_cast<char*>(string));
	}





	template <typename DataType>
	inline ResultStatus ReadArrayAsync(DataType* buffer, uint32 size = 1) {
		return StartReadAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	// Reads one value of DataType into data.
	template <typename DataType>
	inline ResultStatus ReadAsync(DataType &data) {
		return StartReadAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





public:
	// On any status other than ok the previous Parameters are kept, so they match the registers that were left unchanged.
	ResultStatus SetParameters(Parameters val) {
		Parameters previous = parameters;
		parameters = val;
		ResultStatus status = Initialization();
		if(status != ResultStatus::ok) {
			parameters = previous;
		}
		return status;
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	void SetTimeout(uint32 val) {
		timeout = val;
	}


	ResultStatus SetContinuousAsyncTxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncTxMode() : StopContinuousAsyncTxMode();
		if(status == ResultStatus::ok) {
			continuousAsyncTxMode = mode;
		}
		return status;
	}


	// The mode is set before the port starts, because the receive interrupt can run as soon as it is enabled, and it reads the
	// mode to choose between the callback and a read buffer. The mode is cleared after the port stops, so an interrupt that runs
	// during the stop still takes the callback path. A start that fails clears the mode again.
	ResultStatus SetContinuousAsyncRxMode(bool mode) {
		if(!mode) {
			ResultStatus stopStatus = StopContinuousAsyncRxMode();
			if(stopStatus == ResultStatus::ok) {
				continuousAsyncRxMode = false;
			}
			return stopStatus;
		}

		if(continuousAsyncRxMode) {
			return ResultStatus::notAvailable;
		}

		if(rxState != ResultStatus::ready) {
			return ResultStatus::busy;
		}

		continuousAsyncRxMode = true;
		ResultStatus startStatus = StartContinuousAsyncRxMode();
		if(startStatus != ResultStatus::ok) {
			continuousAsyncRxMode = false;
		}
		return startStatus;
	}


	uint16 GetLastRxData() {
		return lastRxData;
	}


	virtual void IrqHandler() = 0;


	virtual void AbortReceive() = 0;
	virtual void AbortTransmit() = 0;


	virtual void AbortAll() {
		AbortTransmit();
		AbortReceive();
	}


	virtual inline ResultStatus GetRxState() {
		return rxState;
	}

	virtual inline ResultStatus GetTxState() {
		return txState;
	}

	virtual inline uint32 GetRxDataCounter() {
		return rxDataCounter;
	}

	virtual inline uint32 GetTxDataCounter() {
		return txDataCounter;
	}



protected:
	virtual ResultStatus Initialization() = 0;

	virtual ResultStatus WriteByteArray(uint8* buffer, uint32 size) = 0;
	virtual ResultStatus ReadByteArray(uint8* buffer, uint32 size) = 0;
	virtual ResultStatus WriteByteArrayAsync(uint8* buffer, uint32 size) = 0;
	virtual ResultStatus ReadByteArrayAsync(uint8* buffer, uint32 size) = 0;


	// Every asynchronous call returns invalidParameter for a size of 0 and sends or receives nothing. Size 0 is refused
	// here, so no port starts a transfer that has no byte.
	inline ResultStatus StartWriteAsync(uint8* buffer, uint32 size) {
		if (size == 0) {
			return ResultStatus::invalidParameter;
		}

		return WriteByteArrayAsync(buffer, size);
	}


	inline ResultStatus StartReadAsync(uint8* buffer, uint32 size) {
		if (size == 0) {
			return ResultStatus::invalidParameter;
		}

		return ReadByteArrayAsync(buffer, size);
	}


	virtual ResultStatus StartContinuousAsyncRxMode() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StopContinuousAsyncRxMode() {
		return ResultStatus::notSupported;
	}

	// Continuous TX is not implemented on any port: the default is notSupported.
	virtual ResultStatus StartContinuousAsyncTxMode() {
		return ResultStatus::notSupported;
	}

	virtual ResultStatus StopContinuousAsyncTxMode() {
		return ResultStatus::notSupported;
	}


	virtual inline void CallInterrupt(Irq irqType) {
		if(onInterrupt != nullptr) {
			onInterrupt(irqType);
		}
	 }

	virtual inline void CallError(Error error) {
		if(onError != nullptr) {
			onError(error);
		}
	 }



};















