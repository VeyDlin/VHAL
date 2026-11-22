#pragma once
#include "ITIMHelper.h"


template <RealType Type = float>
class TIMOutputCompareHelper: public ITIMHelper<Type> {
	using Base = ITIMHelper<Type>;
	using Base::timAdapter;
	using Base::timChannel;
	using Base::CalculateFrequency;
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
		using Wide = FrequencyArithmetic<Type>;

		Wide division = static_cast<Wide>(static_cast<int>(timAdapter->GetClockDivision()));
		Wide period = static_cast<Wide>(timAdapter->GetParameters().period) + Wide(1);
		Wide clockHz = static_cast<Wide>(timAdapter->GetBusClockHz());

		// Ticks per second at prescaler 0. The register holds the divisor minus one, so the value is kept in [0, 0xFFFF]:
		// a negative or zero target gives 0, and a target below the reach of 16 bits gives 0xFFFF.
		Wide ticksPerSecond = clockHz / division / period;
		Wide prescaler = ticksPerSecond / static_cast<Wide>(frequencyHz) - Wide(1);

		uint16 value = 0;
		if (prescaler >= Wide(0xFFFF)) {
			value = 0xFFFF;
		} else if (prescaler > Wide(0)) {
			value = static_cast<uint16>(prescaler);
		}

		SetPrescaler(value);
		return *this;
	}


	TIMOutputCompareHelper& SetHalfCompare() {
		auto period = timAdapter->GetParameters().period + 1;

		uint32 compare = period / 2;
		compare = compare <= 0 ? 1 : compare;

		SetCompare(compare);
		return *this;
	}


	TIMOutputCompareHelper& TuneHalfCompare(uint32 frequencyHz) {
		TunePrescaler(Type(static_cast<int>(frequencyHz * 2)));
		SetHalfCompare();
		return *this;
	}


	inline TIMOutputCompareHelper& EnableCounter(bool enableTimerCounter) {
		timAdapter->EnableCounter(enableTimerCounter);
		return *this;
	}


	TIMOutputCompareHelper& SetFrequencyInfo(const FrequencyInfo& info) {
		using Wide = FrequencyArithmetic<Type>;

		auto [prescaler, period] = CalculateFrequency(timAdapter, info.frequencyHz);

		// The counts are in Wide: period + 1 wraps in uint32 for a 32-bit period of 0xFFFFFFFF. A pulse of 100 % at the
		// slowest output is one tick more than the compare register holds, so it is clamped to the register's maximum.
		// A pulse below one tick is written as one tick.
		bool is32Bit = timAdapter->GetBitness() == ATIM::Bitness::B32;
		uint32 maxCompare = is32Bit ? 0xFFFFFFFFu : 0xFFFFu;
		Wide ticks = (static_cast<Wide>(period) + Wide(1)) * static_cast<Wide>(info.duty) / Wide(100);

		uint32 pulse = 1;
		if (ticks >= static_cast<Wide>(maxCompare)) {
			pulse = maxCompare;
		} else if (ticks >= Wide(1)) {
			pulse = static_cast<uint32>(ticks);
		}

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

};
