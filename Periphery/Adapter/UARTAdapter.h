#pragma once
#include "IAdapter.h"

#define AUSED_UART_ADAPTER



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
	USART_TypeDef *uartHandle;
	Parameters parameters;


	uint32 timeout = 1000;


	bool continuousAsyncRxMode = false;
	Status::statusType rxState = Status::ready;
	uint16 rxDataNeed = 0;
	uint16 rxDataCounter = 0;
	uint8 *rxDataPointer = nullptr;
	uint8 lastRxData = 0;


	bool continuousAsyncTxMode = false;
	Status::statusType txState = Status::ready;
	uint16 txDataNeed = 0;
	uint16 txDataCounter = 0;
	uint8 *txDataPointer = nullptr;




public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;





public:
	UARTAdapter() = default;
	UARTAdapter(USART_TypeDef *uart): uartHandle(uart) { }





	template <typename DataType>
	inline Status::statusType Write(DataType data) {
		return WriteByteArray(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}


	template <typename DataType>
	inline Status::statusType WriteArray(DataType* buffer, uint32 size) {
		return WriteByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType>
	inline Status::statusType WriteArray(const DataType* buffer, uint32 size) {
		return WriteByteArray(const_cast<uint8*>(reinterpret_cast<const uint8*>(buffer)), sizeof(DataType) * size);
	}


	Status::statusType WriteString(char *string) {
		size_t size = 0;
		while(string[size]) {
			size++;
		}

		return WriteByteArray(reinterpret_cast<uint8*>(string), size);
	}


	inline Status::statusType WriteString(const char* string) {
		return WriteString(const_cast<char*>(string));
	}


	template <typename DataType>
	Status::statusType ReadArray(DataType* buffer, uint32 size = 1) {
		return ReadByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType>
	inline Status::info<DataType> Read(uint32 size = 1) {
		auto output = Status::info<DataType>();
		output.type = ReadByteArray(reinterpret_cast<uint8*>(&output.data), sizeof(DataType) * size);
		return output;
	}





	// ---------------





	template <typename DataType>
	inline Status::statusType WriteAsync(DataType &data) {
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





	template <typename DataType>
	Status::statusType WriteArrayAsync(DataType* buffer, uint32 size) {
		return WriteByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline Status::statusType WriteArrayAsync(const DataType* buffer, uint32 size) {
		return WriteArrayAsync<DataType>(buffer, size);
	}





	inline Status::statusType WriteStringAsync(char *string) {
		size_t size = 0;
		while(string[size]) {
			size++;
		}

		return WriteByteArrayAsync(reinterpret_cast<uint8*>(string), size);
	}





	inline Status::statusType WriteStringAsync(const char* string) {
		return WriteStringAsync(const_cast<char*>(string));
	}





	template <typename DataType>
	inline Status::statusType ReadArrayAsync(DataType* buffer, uint32 size = 1) {
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}





	template <typename DataType>
	inline Status::statusType ReadAsync(DataType &data, uint32 size = 1) {
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType) * size);
	}





public:
	void SetParameters(Parameters val) {
		parameters = val;
		Initialization();
	}


	void SetTimeout(uint32 val) {
		timeout = val;
	}


	Status::statusType SetContinuousAsyncTxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncTxMode() : StopContinuousAsyncTxMode();
		if(status == Status::ok) {
			continuousAsyncTxMode = mode;
		}
		return status;
	}


	Status::statusType SetContinuousAsyncRxMode(bool mode) {
		auto status = mode ? StartContinuousAsyncRxMode() : StopContinuousAsyncRxMode();
		if(status == Status::ok) {
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


	virtual inline Status::statusType GetRxState() {
		return rxState;
	}

	virtual inline Status::statusType GetTxState() {
		return txState;
	}

	virtual inline uint16 GetRxDataCounter() {
		return rxDataCounter;
	}

	virtual inline uint16 GetTxDataCounter() {
		return txDataCounter;
	}



protected:
	virtual Status::statusType Initialization() = 0;

	virtual Status::statusType WriteByteArray(uint8* buffer, uint32 size) = 0;
	virtual Status::statusType ReadByteArray(uint8* buffer, uint32 size) = 0;
	virtual Status::statusType WriteByteArrayAsync(uint8* buffer, uint32 size) = 0;
	virtual Status::statusType ReadByteArrayAsync(uint8* buffer, uint32 size) = 0;

	virtual Status::statusType StartContinuousAsyncRxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StopContinuousAsyncRxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StartContinuousAsyncTxMode() {
		return Status::notSupported;
	}

	virtual Status::statusType StopContinuousAsyncTxMode() {
		return Status::notSupported;
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















