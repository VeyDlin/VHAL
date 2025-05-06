#pragma once
#include "IAdapter.h"
#include "UniqueCode/IUniqueCode.h"
#include <initializer_list>

#include <System/System.h>

#define AUSED_TIM_ADAPTER



class TIMAdapter: public IAdapter {
	static constexpr uint8 maxChannels = 8;

public:
	AUNIQUECODE_STRUCT_U32(ClockDivisionMode);
	AUNIQUECODE_STRUCT_U32(CounterMode);
	AUNIQUECODE_STRUCT_U32(OutputCompareMode);
	AUNIQUECODE_STRUCT_U32(OutputTriggerMode);
	AUNIQUECODE_STRUCT_U32(OutputTriggerMode2);

	AUNIQUECODE_DOUBLE_STRUCT_U32(ChannelMode);

	AUNIQUECODE_STRUCT_U8(InterruptType);


	enum class State { Enable, Disable };
	enum class Polarity { Low, High };
	enum class IdleState { Low, High };
	enum class ChannelSelect { Positive, Negative, PositiveNegative, NoOutput };
	enum class ChannelEnableSelect { EnablePositive, EnableNegative, EnableAll, DisablePositive, DisableNegative, DisableAll };
	enum class Bitness { B16, B32 };


	struct Parameters {
		const ClockDivisionMode *division;
		uint16 prescaler = 0;
		uint32 period = 1;
		const CounterMode *mode;
		uint16 repetitionCounter = 0;
		const OutputTriggerMode *outputTrigger = nullptr;
		const OutputTriggerMode2 *outputTrigger2 = nullptr;
		// TODO: MasterSlaveMode
	};

	struct OutputCompareParameters {
		const ChannelMode *channel;
		const OutputCompareMode *mode;
		uint32 compare = 0;
		ChannelSelect channelSelect = ChannelSelect::Positive;
		struct {
			Polarity polarity = Polarity::High;
			State state = State::Disable;
			IdleState idleState = IdleState::Low;
		} positive;
		struct {
			Polarity polarity = Polarity::High;
			State state = State::Disable;
			IdleState idleState = IdleState::Low;
		} negative;
	};

	struct InputCaptureParameters {

	};

	struct EncoderParameters {

	};

	struct HallSensorParameters {

	};

	struct BreakAndDeadTimeParameters {

	};


	struct EnableChannelParameters {
		const ChannelMode *channel;
		ChannelEnableSelect channelSelect = ChannelEnableSelect::EnablePositive;
	};


protected:
	TIM_TypeDef *timHandle;
	Parameters parameters;
	OutputCompareParameters outputCompareParameters[maxChannels];
	BreakAndDeadTimeParameters breakAndDeadTimeParameters[maxChannels];

	uint32 inputBusClockHz = 0;
	Bitness bitness = Bitness::B16;


public:
	Function<void(uint8 channel)> onInputCaptureEvent;
	Function<void(uint8 channel)> onOutputCompareEvent;
	Function<void()> onUpdateEvent;
	Function<void()> onBreakInputEvent;
	Function<void()> onBreak2InputEvent;
	Function<void()> onTriggerDetectionEvent;
	Function<void()> onCommutationEvent;


public:
	TIMAdapter() { }
	TIMAdapter(TIM_TypeDef *tim, uint32 busClockHz, Bitness bit = Bitness::B16): timHandle(tim), inputBusClockHz(busClockHz), bitness(bit) { }



	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}





	const Parameters& GetParameters() {
		return parameters;
	}


	const OutputCompareParameters& GetOutputCompareParameters(const ChannelMode *channel) {
		return outputCompareParameters[GetChannelIndex(channel)];
	}


	const BreakAndDeadTimeParameters& GetBreakAndDeadTimeParameters(const ChannelMode *channel) {
		return breakAndDeadTimeParameters[GetChannelIndex(channel)];
	}


	uint32 GetBusClockHz() {
		return inputBusClockHz;
	}


	virtual Bitness GetBitness() {
		return bitness;
	}




	virtual Status::statusType ConfigOutputCompareParameters(const std::initializer_list<OutputCompareParameters>& list) {
		uint8 i = 0;
		for(auto &channel : list) {
			outputCompareParameters[i++] = channel;
			SystemAssert(i <= maxChannels);
		}
	    return OutputCompareInitialization(list);
	}



	virtual Status::statusType ConfigBreakAndDeadTimeParameters(const std::initializer_list<BreakAndDeadTimeParameters>& list) {
		uint8 i = 0;
		for(auto &channel : list) {
			breakAndDeadTimeParameters[i++] = channel;
			SystemAssert(i <= maxChannels);
		}
	    return BreakAndDeadTimeInitialization(list);
	}



	virtual Status::statusType EnableInterrupt(const std::initializer_list<const InterruptType*>& list) {
		// TODO: Add save
		for(auto &channel : list) {
			auto status = SetInterrupt(channel, true);
			if(status != Status::ok) {
				return status;
			}
		}
		return Status::ok;
	}



	virtual Status::statusType DisableInterrupt(const std::initializer_list<const InterruptType*>& list) {
		// TODO: Add save
		for(auto &channel : list) {
			auto status = SetInterrupt(channel, false);
			if(status != Status::ok) {
				return status;
			}
		}
		return Status::ok;
	}


	virtual inline void EnableCounter(bool enableTimerCounter) = 0;
	virtual inline void SetChannelsState(const std::initializer_list<EnableChannelParameters>& list) = 0;

	virtual inline void SetDivision(const ClockDivisionMode *division) = 0;
	virtual inline void SetPrescaler(uint32 prescaler) = 0;
	virtual inline void SetPeriod(uint32 period) = 0;
	virtual inline void SetCompare(const ChannelMode *channel, uint32 compare) = 0;
	virtual inline void GenerateUpdateEvent() = 0;

	virtual inline void IrqHandler() = 0;

	virtual uint16 GetClockDivision() = 0;

	virtual uint8 GetChannelIndex(const ChannelMode *channel) = 0;





protected:
	virtual Status::statusType Initialization() = 0;
	virtual Status::statusType OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) = 0;
	virtual Status::statusType BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>& list) = 0;
	virtual Status::statusType SetInterrupt(const InterruptType *interrupt, bool enable) = 0;


	virtual inline void CallInputCaptureEvent(uint8 channel) {
		if(onInputCaptureEvent != nullptr) {
			onInputCaptureEvent(channel);
		}
	}


	virtual inline void CallOutputCompareEvent(uint8 channel) {
		if(onOutputCompareEvent != nullptr) {
			onOutputCompareEvent(channel);
		}
	}


	virtual inline void CallUpdateEvent() {
		if(onUpdateEvent != nullptr) {
			onUpdateEvent();
		}
	}


	virtual inline void CallBreakInputEvent() {
		if(onBreakInputEvent != nullptr) {
			onBreakInputEvent();
		}
	}


	virtual inline void CallBreak2InputEvent() {
		if(onBreak2InputEvent != nullptr) {
			onBreak2InputEvent();
		}
	}


	virtual inline void CallTriggerDetectionEvent() {
		if(onTriggerDetectionEvent != nullptr) {
			onTriggerDetectionEvent();
		}
	}


	virtual inline void CallCommutationEvent() {
		if(onCommutationEvent != nullptr) {
			onCommutationEvent();
		}
	}

};















