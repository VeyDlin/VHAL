#pragma once
#include "IAdapter.h"

#define VHAL_DMA_ADAPTER


template<typename HandleType>
class DMAAdapter : public IAdapter {
public:
	struct DMAPeripheralOption : IOption<uint32> {
		using IOption::IOption;
	};

	enum class DataWidth : uint8 { B8 = 8, B16 = 16, B32 = 32 };

	enum class Direction {
		MemoryToPeripheral,
		PeripheralToMemory,
		MemoryToMemory
	};

	enum class AddressMode {
		FixedToIncrementing,    	// peripheral -> memory (RX)
		IncrementingToFixed,    	// memory -> peripheral (TX)
		IncrementingToIncrementing 	// memory -> memory
	};

	struct Parameters {
		DMAPeripheralOption peripheral;
		Direction direction;
		AddressMode addressMode;
		bool circularMode = false;
		bool enableTransferCompleteIT = true;
		DataWidth dataWidth = DataWidth::B8;
		uint32 priority = 0;
	};


protected:
	HandleType *dmaHandle;
	uint32 dmaChannel;
	Parameters parameters;

	uint32 lastTransferSize = 0;

public:
	std::function<void()> onTransferComplete;
	std::function<void()> onHalfTransfer;
	std::function<void()> onError;

public:
	DMAAdapter() = default;

	DMAAdapter(HandleType *dma, uint32 channel)
		: dmaHandle(dma), dmaChannel(channel) { }

	ResultStatus SetParameters(const Parameters &params) {
		parameters = params;
		return Initialization();
	}

	virtual void IrqHandler() = 0;

	virtual ResultStatus GetStatus() = 0;

	virtual uint32 GetLastTransferSize() const {
		return lastTransferSize;
	}

	template <typename T>
	ResultStatus Start(const T* from, T* to, uint32 count) {
		return StartTransfer(
			reinterpret_cast<const uint8*>(from),
			reinterpret_cast<uint8*>(to),
			sizeof(T) * count
		);
	}

	virtual ResultStatus Stop() = 0;

protected:
	virtual ResultStatus StartTransfer(const uint8* from, uint8* to, uint32 size) = 0;
	virtual ResultStatus Initialization() = 0;

	inline void CallTransferComplete() {
		if (onTransferComplete) {
			onTransferComplete();
		}
	}

	inline void CallHalfTransfer() {
		if (onHalfTransfer) {
			onHalfTransfer();
		}
	}

	inline void CallError() {
		if (onError) {
			onError();
		}
	}
};
