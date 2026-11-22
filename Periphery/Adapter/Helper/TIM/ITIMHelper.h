#pragma once
#include <Adapter/TIMAdapter.h>
#include <Utilities/Math/IQMath/IQ.h>
#include <type_traits>
#include <utility>


struct TimerChannel {
	ATIM *timer;
	ATIM::ChannelOption channel;
};


// The arithmetic of a floating frequency search. A float target stays in float, so a float build does not link the double
// soft-float helpers. An IQ target does not use this type: its search is integer, on the raw value (see IQCounts).
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
	// Period register value for counts timer ticks per cycle, for a float target: counts - 1, kept in [1, maxPeriod].
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


	// The same for an integer count of ticks, used by an IQ target. A count below 2 gives period 1.
	static uint32 PeriodFromRawCounts(uint64 counts, uint32 maxPeriod) {
		if (counts < 2) {
			return 1;
		}

		uint64 period = counts - 1;

		if (period >= maxPeriod) {
			return maxPeriod;
		}

		return static_cast<uint32>(period);
	}


	// Timer ticks per cycle for an IQ target: floor(clock * 2^q / (division * divisor * raw)), where raw is the target in
	// units of 2^-q Hz. Two integer divisions give the same floor as one division by the product, and keep every
	// intermediate small. Overflow: clock << q < 2^62, because clock < 2^32 and q <= 30. division * divisor < 2^34, because
	// division <= 4 and divisor <= 2^32. The raw divisor is not multiplied by anything, so it can be any uint64 that is not 0.
	static uint64 IQCounts(uint32 clockHz, uint32 division, uint64 divisor, uint64 raw, int q) {
		uint64 scaled = static_cast<uint64>(clockHz) << q;

		return scaled / (static_cast<uint64>(division) * divisor) / raw;
	}


	// An int32 raw value from a 64-bit one, saturated at the largest positive value.
	static int32 SaturateToRaw(uint64 value) {
		if (value > 0x7FFFFFFFu) {
			return 0x7FFFFFFF;
		}

		return static_cast<int32>(value);
	}


	// The bus clock is in Hz. A float target searches in FrequencyArithmetic, and an IQ target searches on its raw value in
	// integers, so no double is used for an IQ target.
	static std::pair<uint16, uint32> CalculateFrequency(ATIM *timAdapter, Type targetFrequency) {
		if constexpr (IQType<Type>) {
			return CalculateFrequencyRaw(timAdapter, targetFrequency.raw);
		} else {
			return CalculateFrequencyFloat(timAdapter, targetFrequency);
		}
	}


	// The raw value is the target in units of 2^-q Hz. A zero raw value gives the slowest output, which is what the float
	// path gives for a zero target. A negative one gives the fastest output (prescaler 0, period 1), as the float path does.
	// 16-bit: counts(p) = clock * 2^q / (division * (p + 1) * raw) is below 65537 exactly when p + 1 > X / (raw * 65537),
	// where X = floor(clock * 2^q / division). The smallest such p is floor(X / (raw * 65537)), so no search runs.
	// Overflow: raw < 2^31, so raw * 65537 < 2^48.
	static std::pair<uint16, uint32> CalculateFrequencyRaw(ATIM *timAdapter, int32 raw) {
		uint32 clockHz = timAdapter->GetBusClockHz();
		uint32 division = static_cast<uint32>(static_cast<int>(timAdapter->GetClockDivision()));
		bool is32Bit = timAdapter->GetBitness() == ATIM::Bitness::B32;

		if (raw < 0) {
			return { 0, 1 };
		}

		if (raw == 0) {
			if (is32Bit) {
				return { 0, 0xFFFFFFFFu };
			}

			return { 0xFFFF, 0xFFFFu };
		}

		uint64 target = static_cast<uint64>(raw);

		if (is32Bit) {
			return { 0, PeriodFromRawCounts(IQCounts(clockHz, division, 1, target, Type::q), 0xFFFFFFFFu) };
		}

		uint64 candidate = ((static_cast<uint64>(clockHz) << Type::q) / division) / (target * 65537u);

		if (candidate > 0xFFFF) {
			return { 0xFFFF, 0xFFFFu };
		}

		uint64 counts = IQCounts(clockHz, division, candidate + 1, target, Type::q);

		return { static_cast<uint16>(candidate), PeriodFromRawCounts(counts, 0xFFFFu) };
	}


	// A float target. The counts are in FrequencyArithmetic, and the prescaler rises until the period fits in 16 bits.
	static std::pair<uint16, uint32> CalculateFrequencyFloat(ATIM *timAdapter, Type targetFrequency) {
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
