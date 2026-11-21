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
		Direction direction = Direction::MemoryToPeripheral;
		AddressMode addressMode = AddressMode::IncrementingToFixed;
		bool circularMode = false;
		bool enableTransferCompleteIT = true;
		DataWidth dataWidth = DataWidth::B8;
		DataWidth periphDataWidth = static_cast<DataWidth>(0);
		uint32 priority = 0;
	};


protected:
	HandleType *dmaHandle;
	uint32 dmaChannel;
	Parameters parameters;

	// The run mode of the transfer that was started last. The interrupt and the status read this copy, so a SetParameters
	// during a run cannot change how the run ends.
	volatile bool runCircular = false;

	uint32 lastTransferSize = 0;

public:
	std::function<void()> onTransferComplete;
	std::function<void()> onHalfTransfer;
	std::function<void()> onError;

public:
	DMAAdapter()
		: dmaHandle(nullptr), dmaChannel(0) { }

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


	const Parameters &GetParameters() const {
		return parameters;
	}


	// The width of the peripheral side. Zero in periphDataWidth means the same width as the memory side.
	constexpr DataWidth GetPeriphDataWidth() const {
		return (parameters.periphDataWidth != static_cast<DataWidth>(0))
			? parameters.periphDataWidth
			: parameters.dataWidth;
	}


	// The transfer counter is defined only when both sides have the same width. A pair that differs is refused by the
	// ports at the start, so a count is the same number of items on the peripheral side and the memory side.
	ResultStatus CheckWidths() const {
		return (GetPeriphDataWidth() == parameters.dataWidth)
			? ResultStatus::ok
			: ResultStatus::invalidParameter;
	}


	// The run mode of the transfer that was started last. A running channel is read through this, not through the parameters.
	bool IsRunCircular() const {
		return runCircular;
	}


	// Checks that the channel is set up for the peripheral request, direction and width the caller expects. Both sides
	// use the same width: the peripheral register and the caller's buffer element.
	ResultStatus CheckPeripheral(uint32 request, Direction direction, DataWidth width) const {
		if (parameters.peripheral.Get() != request) {
			return ResultStatus::invalidParameter;
		}

		if (parameters.direction != direction) {
			return ResultStatus::invalidParameter;
		}

		if (GetPeriphDataWidth() != width || parameters.dataWidth != width) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::ok;
	}


	// Converts a size in bytes into a count of items of itemBytes each. A size of zero, a size that is not a whole number
	// of items and a count above maxCount are refused.
	static ResultStatus ItemCount(uint32 sizeBytes, uint32 itemBytes, uint32 maxCount, uint32 &count) {
		count = 0;

		if (itemBytes == 0 || sizeBytes == 0 || sizeBytes % itemBytes != 0) {
			return ResultStatus::invalidParameter;
		}

		uint32 items = sizeBytes / itemBytes;
		if (items > maxCount) {
			return ResultStatus::invalidParameter;
		}

		count = items;
		return ResultStatus::ok;
	}


	// Status rules shared by the ports. An error latched by the interrupt or seen in the flags wins. A circular transfer
	// never completes: it is ok while the channel runs and stop once it is off. A normal transfer is ok after its
	// completion is latched, busy while the channel runs, and stop when the channel is off before the completion.
	static ResultStatus LatchedStatus(bool errorLatched, bool completeLatched, bool circular, bool channelEnabled) {
		if (errorLatched) {
			return ResultStatus::error;
		}

		if (circular) {
			return channelEnabled ? ResultStatus::ok : ResultStatus::stop;
		}

		if (completeLatched) {
			return ResultStatus::ok;
		}

		return channelEnabled ? ResultStatus::busy : ResultStatus::stop;
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

	// Called by a port when a transfer starts, after the channel accepted it.
	void LatchRunMode() {
		runCircular = parameters.circularMode;
	}

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
