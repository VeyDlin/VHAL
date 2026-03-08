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
		Type division = Type(static_cast<int>(timAdapter->GetClockDivision()));
		Type period = Type(static_cast<int>(timAdapter->GetParameters().period + 1));
		Type sourceFrequency = Type(static_cast<int>(timAdapter->GetBusClockHz())) * Type(1000);

		Type input = sourceFrequency / division / period;

		Type prescaler = input / frequencyHz;
		prescaler = prescaler > period ? period - Type(1) : prescaler;

		SetPrescaler(static_cast<uint16>(static_cast<uint32>(prescaler)));
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
		auto [prescaler, period] = CalculateFrequency(timAdapter, info.frequencyHz);

		uint32 pulse = static_cast<uint32>(Type(static_cast<int>(period + 1)) * info.duty / Type(100));
		if (pulse == 0) {
		    pulse = 1;
		}

		System::CriticalSection(true);

		SetPrescaler(prescaler);
		SetPeriod(period);

		if (timAdapter->GetBitness() == ATIM::Bitness::B32) {
		    SetCompare(static_cast<uint32>(pulse));
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
		Type division = Type(static_cast<int>(timAdapter->GetClockDivision()));
		Type prescaler = Type(static_cast<int>(timAdapter->GetParameters().prescaler));
		Type period = Type(static_cast<int>(timAdapter->GetParameters().period));
		Type sourceFrequency = Type(static_cast<int>(timAdapter->GetBusClockHz())) * Type(1000);
		Type compare = Type(static_cast<int>(timAdapter->GetOutputCompareParameters(timChannel).compare));

		return FrequencyInfo{
			.frequencyHz = sourceFrequency / division / (prescaler + Type(1)) / (period + Type(1)),
			.duty = (compare / (period + Type(1))) * Type(100)
		};
	}

};
