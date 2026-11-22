#pragma once
// Host stand-in for the TIM adapter. The bus clock is in Hz, as the real adapter reports it. The prescaler, period and
// compare that a helper writes are kept, so a test can read back what the helper set. There are no registers and no interrupts.
#include <Utilities/DataTypes.h>
#include <initializer_list>
#include <utility>

namespace System {
	inline void CriticalSection(bool) {}
}

class ATIM {
public:
	enum class Bitness { B16, B32 };
	enum class ClockDivisionOption { Div1 = 1, Div2 = 2, Div4 = 4 };
	enum class ChannelOption { Ch1, Ch2 };
	enum class OutputCompareOption { Pwm1 };
	enum class ChannelSelect { Positive, Negative, PositiveNegative, NoOutput };
	enum class ChannelEnableSelect { EnablePositive, EnableNegative, EnableAll, DisablePositive, DisableNegative, DisableAll };

	struct ChannelState {
		ChannelOption channel;
		ChannelEnableSelect select;
	};

	struct Parameters {
		uint16 prescaler = 0;
		uint32 period = 1;
	};

	struct OutputCompareParameters {
		ChannelSelect channelSelect = ChannelSelect::Positive;
		uint32 compare = 0;
	};


private:
	uint32 busClockHz = 0;
	Bitness bitness = Bitness::B16;
	ClockDivisionOption division = ClockDivisionOption::Div1;
	Parameters parameters;
	OutputCompareParameters compareParameters;


public:
	ATIM(uint32 clockHz, Bitness bit, ClockDivisionOption div) : busClockHz(clockHz), bitness(bit), division(div) {}


	uint32 GetBusClockHz() { return busClockHz; }
	Bitness GetBitness() { return bitness; }
	ClockDivisionOption GetClockDivision() { return division; }
	Parameters GetParameters() { return parameters; }
	OutputCompareParameters GetOutputCompareParameters(ChannelOption) { return compareParameters; }
	uint32 GetCapture(ChannelOption) { return 0; }
	uint8 GetChannelIndex(ChannelOption) { return 0; }


	void SetChannelsState(std::initializer_list<ChannelState>) {}
	void SetDivision(ClockDivisionOption) {}
	void SetPrescaler(uint32 value) { parameters.prescaler = static_cast<uint16>(value); }
	void SetPeriod(uint32 value) { parameters.period = value; }
	void SetCompare(ChannelOption, uint32 value) { compareParameters.compare = value; }
	void SetCompareMode(ChannelOption, OutputCompareOption) {}
	void GenerateUpdateEvent() {}
	void EnableCounter(bool) {}
};
