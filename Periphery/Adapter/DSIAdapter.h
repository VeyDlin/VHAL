#pragma once
#include "IAdapter.h"

#define VHAL_DSI_ADAPTER


template<typename HandleType>
class DSIAdapter : public IAdapter {
public:
	enum class Mode { Video, Command };
	enum class Polarity { Low, High };

	enum class Irq { RefreshDone, TearingEffect, TransferDone };
	enum class Error { None, Transmission, Protocol, Overflow };

	struct ClockSourceOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct ColorFormatOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct VideoTiming {
		uint16 hsyncWidth = 0;
		uint16 hBackPorch = 0;
		uint16 hFrontPorch = 0;
		uint16 activeWidth = 0;
		uint16 vsyncWidth = 0;
		uint16 vBackPorch = 0;
		uint16 vFrontPorch = 0;
		uint16 activeHeight = 0;
	};

	struct Parameters {
		Mode mode = Mode::Video;
		uint8 laneCount = 2;
		uint32 laneBitRateMbps = 500;
		uint8 virtualChannel = 0;
		ColorFormatOption colorFormat;
		VideoTiming timing;
		uint32 pixelClockKHz = 0;
		ClockSourceOption clockSource;
		Polarity hsPolarity = Polarity::Low;
		Polarity vsPolarity = Polarity::Low;
		Polarity dePolarity = Polarity::Low;
		uint8 frameBufferCount = 1;
	};


protected:
	HandleType *dsiHandle;
	Parameters parameters;


public:
	std::function<void(Irq irqType)> onInterrupt;
	std::function<void(Error errorType)> onError;


public:
	DSIAdapter() = default;
	DSIAdapter(HandleType *handle) : dsiHandle(handle) { }


	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// DCS commands
	virtual Status::statusType WriteCommand(uint8 cmd, const uint8 *params = nullptr, uint32 paramSize = 0) = 0;
	virtual Status::statusType ReadCommand(uint8 cmd, uint8 *data, uint32 size) = 0;

	// Control
	virtual Status::statusType Start() = 0;
	virtual Status::statusType Stop() = 0;

	// Framebuffer
	virtual void* GetFrameBuffer(uint8 index = 0) = 0;

	virtual void IrqHandler() = 0;


protected:
	virtual Status::statusType Initialization() = 0;


	virtual inline void CallInterrupt(Irq irqType) {
		if (onInterrupt != nullptr) {
			onInterrupt(irqType);
		}
	}

	virtual inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
