#pragma once
#include "IAdapter.h"

#define VHAL_UART_ADAPTER


template<typename HandleType>
class UARTAdapter: public IAdapter {
public:
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
	ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
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


	ResultStatus SetContinuousAsyncRxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncRxMode() : StopContinuousAsyncRxMode();
		if(status == ResultStatus::ok) {
			continuousAsyncRxMode = mode;
		}
		return status;
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















