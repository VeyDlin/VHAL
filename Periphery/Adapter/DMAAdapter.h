#pragma once
#include "IAdapter.h"

#define AUSED_DMA_ADAPTER


class DMAAdapter {
public:
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
		Direction direction;
		AddressMode addressMode;
		bool circularMode = false;
		uint32 priority = 0;
	};


protected:
	DMA_TypeDef *dmaHandle;
	uint32 dmaChannel;
	uint32 dmaRequest;
	Parameters parameters;

	uint32 lastTransferSize = 0;

public:
	std::function<void()> onTransferComplete;
	std::function<void()> onHalfTransfer;
	std::function<void()> onError;

public:
	DMAAdapter() = default;

	DMAAdapter(DMA_TypeDef *dma, uint32 channel, uint32 request)
		: dmaHandle(dma), dmaChannel(channel), dmaRequest(request) { }

	Status::statusType SetParameters(const Parameters &params) {
		parameters = params;
		return Initialization();
	}

	virtual void IrqHandler() = 0;

	virtual Status::statusType GetStatus() = 0;

	virtual uint32 GetLastTransferSize() const {
		return lastTransferSize;
	}

	template <typename T>
	Status::statusType Start(const T* from, T* to, uint32 count) {
		return StartTransfer(
			reinterpret_cast<const uint8*>(from),
			reinterpret_cast<uint8*>(to),
			sizeof(T) * count
		);
	}

	virtual Status::statusType Stop() = 0;

protected:
	virtual Status::statusType StartTransfer(const uint8* from, uint8* to, uint32 size) = 0;
	virtual Status::statusType Initialization() = 0;

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
