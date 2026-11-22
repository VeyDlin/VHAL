#pragma once
#include "ITIMHelper.h"


template <RealType Type = float>
class TIMOutputCompareHelper: public ITIMHelper<Type> {
	using Base = ITIMHelper<Type>;
	using Base::timAdapter;
	using Base::timChannel;
	using Base::CalculateFrequency;
	using Base::IQCounts;
	using Base::SaturateToRaw;
	using Base::SetPrescaler;
	using Base::SetPeriod;
	using Base::SetCompare;

public:
	struct FrequencyInfo {
		Type frequencyHz;
		Type duty; // Duty in %
	};


public:
	TIMOutputCompareHelper() { }
	TIMOutputCompareHelper(ATIM& adapter, ATIM::ChannelOption channel): Base(adapter, channel) { }
	TIMOutputCompareHelper(TimerChannel& timerChannel) : Base(timerChannel)  { }


	TIMOutputCompareHelper& TunePrescaler(Type frequencyHz) {
		if constexpr (IQType<Type>) {
			// A negative target is clamped to 0, as the float path does.
			if (frequencyHz.raw < 0) {
				SetPrescaler(0);
			} else {
				SetPrescaler(PrescalerForRaw(static_cast<uint64>(frequencyHz.raw)));
			}
		} else {
			using Wide = FrequencyArithmetic<Type>;

			Wide division = static_cast<Wide>(static_cast<int>(timAdapter->GetClockDivision()));
			Wide period = static_cast<Wide>(timAdapter->GetParameters().period) + Wide(1);
			Wide clockHz = static_cast<Wide>(timAdapter->GetBusClockHz());

			// Ticks per second at prescaler 0. The register holds the divisor minus one, so the value is kept in [0, 0xFFFF]:
			// a negative target gives 0, a zero target gives 0xFFFF, and a target below the reach of 16 bits gives 0xFFFF.
			Wide ticksPerSecond = clockHz / division / period;
			Wide prescaler = ticksPerSecond / static_cast<Wide>(frequencyHz) - Wide(1);

			uint16 value = 0;
			if (prescaler >= Wide(0xFFFF)) {
				value = 0xFFFF;
			} else if (prescaler > Wide(0)) {
				value = static_cast<uint16>(prescaler);
			}

			SetPrescaler(value);
		}

		return *this;
	}


	TIMOutputCompareHelper& SetHalfCompare() {
		// The period plus one is taken in 64 bits, so the period 0xFFFFFFFF does not wrap to 0.
		uint64 period = static_cast<uint64>(timAdapter->GetParameters().period) + 1;

		uint64 compare = period / 2;

		if (compare < 1) {
			compare = 1;
		}

		SetCompare(static_cast<uint32>(compare));
		return *this;
	}


	TIMOutputCompareHelper& TuneHalfCompare(uint32 frequencyHz) {
		// The target is twice the frequency. For an IQ type it is shifted into raw units in 64 bits, so a target above the
		// range of the type (iq16 ends at 32767 Hz) is not saturated before the search. Overflow: 2 * frequencyHz < 2^33,
		// and shifted by q <= 30 it is below 2^63.
		uint64 twiceHz = static_cast<uint64>(frequencyHz) * 2;

		if constexpr (IQType<Type>) {
			SetPrescaler(PrescalerForRaw(twiceHz << Type::q));
		} else {
			TunePrescaler(static_cast<Type>(twiceHz));
		}

		SetHalfCompare();
		return *this;
	}


	inline TIMOutputCompareHelper& EnableCounter(bool enableTimerCounter) {
		timAdapter->EnableCounter(enableTimerCounter);
		return *this;
	}


	TIMOutputCompareHelper& SetFrequencyInfo(const FrequencyInfo& info) {
		auto [prescaler, period] = CalculateFrequency(timAdapter, info.frequencyHz);

		// A pulse of 100 % at the slowest output is one tick more than the compare register holds, so it is clamped to the
		// register's maximum. A pulse below one tick is written as one tick.
		bool is32Bit = timAdapter->GetBitness() == ATIM::Bitness::B32;
		uint32 maxCompare = is32Bit ? 0xFFFFFFFFu : 0xFFFFu;
		uint32 pulse = PulseFromDuty(period, info.duty, maxCompare);

		System::CriticalSection(true);

		SetPrescaler(prescaler);
		SetPeriod(period);

		if (is32Bit) {
		    SetCompare(pulse);
		} else {
		    SetCompare(static_cast<uint16>(pulse));
		}

		timAdapter->GenerateUpdateEvent();

		System::CriticalSection(false);

		return *this;
	}


	TIMOutputCompareHelper& SetFrequency(Type frequencyHz) {
		SetFrequencyInfo(FrequencyInfo {
			.frequencyHz = frequencyHz,
			.duty = GetFrequencyInfo().duty
		});

	    return *this;
	}


	TIMOutputCompareHelper& SetDuty(Type duty) {
		SetFrequencyInfo(FrequencyInfo {
			.frequencyHz = GetFrequencyInfo().frequencyHz,
			.duty = duty
		});

	    return *this;
	}


	FrequencyInfo GetFrequencyInfo() {
		if constexpr (IQType<Type>) {
			uint32 division = static_cast<uint32>(static_cast<int>(timAdapter->GetClockDivision()));
			uint64 prescaler = timAdapter->GetParameters().prescaler;
			uint64 period = static_cast<uint64>(timAdapter->GetParameters().period) + 1;
			uint64 compare = timAdapter->GetOutputCompareParameters(timChannel).compare;
			uint64 clockScaled = static_cast<uint64>(timAdapter->GetBusClockHz()) << Type::q;

			// Overflow: clockScaled < 2^62. The duty is compare * 100 / period, split into its whole part and remainder so that
			// the raw fraction is floored once: (whole << q) + floor((remainder << q) / period). The whole part is saturated
			// before it is shifted. The remainder is below period, so remainder << q < 2^62.
			uint64 frequencyRaw = clockScaled / division / (prescaler + 1) / period;
			uint64 numerator = compare * 100u;
			uint64 whole = numerator / period;
			uint64 dutyRaw = 0x7FFFFFFFu;

			if (whole <= (0x7FFFFFFFu >> Type::q)) {
				dutyRaw = (whole << Type::q) + (((numerator % period) << Type::q) / period);
			}

			return FrequencyInfo {
				.frequencyHz = Type::FromRaw(SaturateToRaw(frequencyRaw)),
				.duty = Type::FromRaw(SaturateToRaw(dutyRaw))
			};
		} else {
			using Wide = FrequencyArithmetic<Type>;

			Wide division = static_cast<Wide>(static_cast<int>(timAdapter->GetClockDivision()));
			Wide prescaler = static_cast<Wide>(timAdapter->GetParameters().prescaler);
			Wide period = static_cast<Wide>(timAdapter->GetParameters().period);
			Wide clockHz = static_cast<Wide>(timAdapter->GetBusClockHz());
			Wide compare = static_cast<Wide>(timAdapter->GetOutputCompareParameters(timChannel).compare);

			return FrequencyInfo{
				.frequencyHz = Type(clockHz / division / (prescaler + Wide(1)) / (period + Wide(1))),
				.duty = Type((compare / (period + Wide(1))) * Wide(100))
			};
		}
	}


protected:
	// The compare value for a duty: (period + 1) * duty / 100 ticks, floored, and kept in [1, maxCompare].
	static uint32 PulseFromDuty(uint32 period, Type duty, uint32 maxCompare) {
		if constexpr (IQType<Type>) {
			if (duty.raw <= 0) {
				return 1;
			}

			// Overflow: period + 1 <= 2^32 and duty.raw < 2^31, so the product is below 2^63. The divisor 100 << q is below 2^37.
			uint64 ticks = (static_cast<uint64>(period) + 1) * static_cast<uint64>(duty.raw) / (static_cast<uint64>(100) << Type::q);

			if (ticks >= maxCompare) {
				return maxCompare;
			}

			if (ticks >= 1) {
				return static_cast<uint32>(ticks);
			}

			return 1;
		} else {
			using Wide = FrequencyArithmetic<Type>;

			Wide ticks = (static_cast<Wide>(period) + Wide(1)) * static_cast<Wide>(duty) / Wide(100);

			if (ticks >= static_cast<Wide>(maxCompare)) {
				return maxCompare;
			}

			if (ticks >= Wide(1)) {
				return static_cast<uint32>(ticks);
			}

			return 1;
		}
	}


	// The prescaler register for an IQ target in units of 2^-q Hz. The raw target may be above the range of the Type, so it
	// arrives as a 64-bit value. The counts at prescaler 0 are clock * 2^q / (division * period * raw), and the register is
	// that count minus one, kept in [0, 0xFFFF]. A zero target has an infinite count in the float path, so it gives 0xFFFF.
	// Overflow: division * period < 2^34, because period + 1 <= 2^32. The raw value is only a divisor, so no product is formed.
	uint16 PrescalerForRaw(uint64 raw) {
		if (raw == 0) {
			return 0xFFFF;
		}

		uint32 division = static_cast<uint32>(static_cast<int>(timAdapter->GetClockDivision()));
		uint64 period = static_cast<uint64>(timAdapter->GetParameters().period) + 1;
		uint64 counts = IQCounts(timAdapter->GetBusClockHz(), division, period, raw, Type::q);

		if (counts < 2) {
			return 0;
		}

		if (counts - 1 >= 0xFFFF) {
			return 0xFFFF;
		}

		return static_cast<uint16>(counts - 1);
	}

};
