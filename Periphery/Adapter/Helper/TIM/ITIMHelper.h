#pragma once
#include <Adapter/TIMAdapter.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <type_traits>
#include <utility>


struct TimerChannel {
	ATIM *timer;
	ATIM::ChannelOption channel;
};


// The arithmetic of the frequency search. A float target stays in float, so a float build does not link the double
// soft-float helpers. An IQ target uses double, because its raw range and resolution are not enough for the counts.
template <RealType Type>
using FrequencyArithmetic = std::conditional_t<std::is_same_v<Type, float>, float, double>;


template <RealType Type = float>
class ITIMHelper {

protected:
	ATIM *timAdapter;
	ATIM::ChannelOption timChannel;


public:
	ITIMHelper() { }
	ITIMHelper(ATIM& adapter) : timAdapter(&adapter) { }
	ITIMHelper(ATIM& adapter, ATIM::ChannelOption channel) : timAdapter(&adapter), timChannel(channel)  { }
	ITIMHelper(TimerChannel& timerChannel) : timAdapter(timerChannel.timer), timChannel(timerChannel.channel)  { }


	ITIMHelper& SetState(bool isEnable) {
		timAdapter->SetChannelsState({{ timChannel, GetChannelEnableSelect(isEnable) }});
		return *this;
	}


	inline void SetDivision(ATIM::ClockDivisionOption division) {
		timAdapter->SetDivision(division);
	}


	inline void SetPrescaler(uint32 prescaler) {
		timAdapter->SetPrescaler(prescaler);
	}


	inline void SetPeriod(uint32 period) {
		timAdapter->SetPeriod(period);
	}


	inline void SetCompare(uint32 compare) {
		timAdapter->SetCompare(timChannel, compare);
	}


	inline void SetCompareMode(ATIM::OutputCompareOption mode) {
		timAdapter->SetCompareMode(timChannel, mode);
	}


	inline uint32 GetCapture() {
		return timAdapter->GetCapture(timChannel);
	}


	inline ATIM* GetAdapter() const {
        return timAdapter;
    }


	inline ATIM::ChannelOption GetChannel() const {
        return timChannel;
    }


	inline uint8 GetChannelNumber() {
        return timAdapter->GetChannelIndex(timChannel) + 1;
    }


	void SetBaseFrequency(Type targetFrequency) {
		auto [prescaler, period] = CalculateFrequency(timAdapter, targetFrequency);

	    System::CriticalSection(true);

	    SetPrescaler(prescaler);
	    SetPeriod(period);
	    timAdapter->GenerateUpdateEvent();

	    System::CriticalSection(false);
	}



protected:
	// Period register value for counts timer ticks per cycle: counts - 1, kept in [1, maxPeriod].
	// A period of 0 is never used, so fewer than two ticks per cycle give period 1.
	static uint32 PeriodFromCounts(FrequencyArithmetic<Type> counts, uint32 maxPeriod) {
		using Wide = FrequencyArithmetic<Type>;

		Wide period = counts - Wide(1);

		if (!(period >= Wide(1))) {
			return 1;
		}

		// The comparison is in Wide: for a float the maximum 0xFFFFFFFF rounds up to 2^32, and a cast of that would overflow.
		if (period >= static_cast<Wide>(maxPeriod)) {
			return maxPeriod;
		}

		return static_cast<uint32>(period);
	}


	// The bus clock is in Hz, and the search runs in FrequencyArithmetic, so an IQ target cannot saturate the arithmetic.
	static std::pair<uint16, uint32> CalculateFrequency(ATIM *timAdapter, Type targetFrequency) {
		using Wide = FrequencyArithmetic<Type>;

		Wide clockHz = static_cast<Wide>(timAdapter->GetBusClockHz());
		Wide division = static_cast<Wide>(static_cast<int>(timAdapter->GetClockDivision()));
		Wide targetHz = static_cast<Wide>(targetFrequency);

		uint16 prescaler = 0;
		uint32 period = 0;

		if (timAdapter->GetBitness() == ATIM::Bitness::B32) {
			period = PeriodFromCounts(clockHz / (division * targetHz), 0xFFFFFFFFu);
		} else {
			// The counts at prescaler p are clockHz / (division * (p + 1) * targetHz). The period is counts - 1, so it fits
			// in 16 bits when counts < 65537. The smallest p that fits is at least floor(clockHz / (division * targetHz * 65537)),
			// so the search starts there and needs only a few steps.
			Wide fitDivisor = clockHz / (division * targetHz) / Wide(65537);
			uint32 candidate = 0;

			if (fitDivisor >= Wide(0x10000)) {
				candidate = 0x10000;
			} else if (fitDivisor >= Wide(1)) {
				candidate = static_cast<uint32>(fitDivisor);
			}

			bool found = false;

			for (; candidate <= 0xFFFF; ++candidate) {
				Wide counts = clockHz / (division * (static_cast<Wide>(candidate) + Wide(1)) * targetHz);

				if (counts < Wide(65537)) {
					prescaler = static_cast<uint16>(candidate);
					period = PeriodFromCounts(counts, 0xFFFF);
					found = true;
					break;
				}
			}

			// No prescaler brings the period into the 16-bit range: the slowest output the timer can make.
			if (!found) {
				prescaler = 0xFFFF;
				period = 0xFFFF;
			}
		}

		return { prescaler, period };
	}



private:
	ATIM::ChannelEnableSelect GetChannelEnableSelect(bool isEnable) {
		auto select = timAdapter->GetOutputCompareParameters(timChannel).channelSelect;

		if(isEnable) {
			switch (select) {
				case ATIM::ChannelSelect::Positive: 		return ATIM::ChannelEnableSelect::EnablePositive;
				case ATIM::ChannelSelect::Negative: 		return ATIM::ChannelEnableSelect::EnableNegative;
				case ATIM::ChannelSelect::PositiveNegative: return ATIM::ChannelEnableSelect::EnableAll;
				case ATIM::ChannelSelect::NoOutput: 		return ATIM::ChannelEnableSelect::DisableAll;
			}
		} else {
			switch (select) {
				case ATIM::ChannelSelect::Positive: 		return ATIM::ChannelEnableSelect::DisablePositive;
				case ATIM::ChannelSelect::Negative: 		return ATIM::ChannelEnableSelect::DisableNegative;
				case ATIM::ChannelSelect::PositiveNegative: return ATIM::ChannelEnableSelect::DisableAll;
				case ATIM::ChannelSelect::NoOutput: 		return ATIM::ChannelEnableSelect::DisableAll;
			}
		}

		return ATIM::ChannelEnableSelect::DisableAll;
	}
};
