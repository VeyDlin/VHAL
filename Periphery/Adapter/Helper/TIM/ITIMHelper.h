#pragma once
#include <Periphery.h>
#include <Utilities/Status.h>
#include <functional>
#include "../../TIMAdapter.h"




struct TimerChannel {
	ATIM *timer;
	ATIM::ChannelOption channel;
};




class ITIMHelper {
protected:
	ATIM *timAdapter;
	ATIM::ChannelOption timChannel;


public:
	ITIMHelper() { }
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


	inline const ATIM::ChannelOption GetChannel() const {
        return timChannel;
    }


	inline uint8 GetChannelNumber() {
        return timAdapter->GetChannelIndex(timChannel) + 1;
    }


	void SetBaseFrequency(float targetFrequency) {
		auto [prescaler, period] = CalculateFrequency(timAdapter, targetFrequency);

	    System::CriticalSection(true);

	    SetPrescaler(prescaler);
	    SetPeriod(period);
	    timAdapter->GenerateUpdateEvent();

	    System::CriticalSection(false);
	}



protected:
	static std::pair<uint16, uint32> CalculateFrequency(ATIM *timAdapter, float targetFrequency) {
	    float sourceFrequency = timAdapter->GetBusClockHz() * 1000;

	    float prescaler = 0;
	    float period = 0;

	    if (timAdapter->GetBitness() == ATIM::Bitness::B32) {
	        period = (sourceFrequency / (timAdapter->GetClockDivision() * targetFrequency)) - 1;
	    } else {
	        for (prescaler = 0; prescaler <= 0xFFFF; ++prescaler) {
	            uint32 tmpPeriod = (sourceFrequency / (timAdapter->GetClockDivision() * (prescaler + 1) * targetFrequency)) - 1;
	            if (tmpPeriod <= 0xFFFF) {
	                period = tmpPeriod;
	                break;
	            }
	        }
	    }

	    if (period == 0) {
	        period = 1;
	    }

	    return {
	    	static_cast<uint16>(prescaler),
	    	timAdapter->GetBitness() == ATIM::Bitness::B32 ? static_cast<uint32>(period) : static_cast<uint16>(period)
	    };
	}



private:
	TIMAdapter::ChannelEnableSelect GetChannelEnableSelect(bool isEnable) {
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
















