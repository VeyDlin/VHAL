#pragma once
#include "ITIMHelper.h"



class TIMOutputCompareHelper: public ITIMHelper {
public:
	struct FrequencyInfo {
		float frequencyHz;
		float duty; // Duty in %
	};


public:
	TIMOutputCompareHelper() { }
	TIMOutputCompareHelper(ATIM& adapter, ATIM::ChannelOption channel): ITIMHelper(adapter, channel) { }
	TIMOutputCompareHelper(TimerChannel& timerChannel) : ITIMHelper(timerChannel)  { }


	TIMOutputCompareHelper& TunePrescaler(float frequencyHz) {
		auto division = timAdapter->GetClockDivision();
		auto period = timAdapter->GetParameters().period + 1;
		auto sourceFrequency = timAdapter->GetBusClockHz() * 1000;

		float input = sourceFrequency / division / period;

		float prescaler = input / frequencyHz;
		prescaler = prescaler > period ? period - 1 : prescaler;

		SetPrescaler(static_cast<uint16>(prescaler));
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
		TunePrescaler(frequencyHz * 2);
		SetHalfCompare();
		return *this;
	}





	inline TIMOutputCompareHelper& EnableCounter(bool enableTimerCounter) {
		timAdapter->EnableCounter(enableTimerCounter);
		return *this;
	}





	TIMOutputCompareHelper& SetFrequencyInfo(const FrequencyInfo& info) {
		auto [prescaler, period] = CalculateFrequency(timAdapter, info.frequencyHz);

		uint32 pulse = static_cast<uint32>(((period + 1) * info.duty) / 100.f);
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





	TIMOutputCompareHelper& SetFrequency(float frequencyHz) {
		SetFrequencyInfo(FrequencyInfo {
			.frequencyHz = frequencyHz,
			.duty = GetFrequencyInfo().duty
		});

	    return *this;
	}





	TIMOutputCompareHelper& SetDuty(float duty) {
		SetFrequencyInfo(FrequencyInfo {
			.frequencyHz = GetFrequencyInfo().frequencyHz,
			.duty = duty
		});

	    return *this;
	}





	FrequencyInfo GetFrequencyInfo() {
		float division = timAdapter->GetClockDivision();
		float prescaler = timAdapter->GetParameters().prescaler;
		float period = timAdapter->GetParameters().period;
		float sourceFrequency = timAdapter->GetBusClockHz() * 1000;
		float compare = timAdapter->GetOutputCompareParameters(timChannel).compare;

		return FrequencyInfo{
			.frequencyHz = sourceFrequency / division / (prescaler + 1) / (period + 1),
			.duty = (compare / (period + 1)) * 100.f
		};
	}

};















