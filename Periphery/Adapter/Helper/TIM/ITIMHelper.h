#pragma once
#include <Adapter/TIMAdapter.h>
#include <Utilities/Math/IQMath/IQ.h>


struct TimerChannel {
	ATIM *timer;
	ATIM::ChannelOption channel;
};


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


	inline const ATIM::ChannelOption GetChannel() const {
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
	static std::pair<uint16, uint32> CalculateFrequency(ATIM *timAdapter, Type targetFrequency) {
	    Type sourceFrequency = Type(static_cast<int>(timAdapter->GetBusClockHz())) * Type(1000);

	    uint32 prescaler = 0;
	    uint32 period = 0;

	    if (timAdapter->GetBitness() == ATIM::Bitness::B32) {
	        period = static_cast<uint32>(sourceFrequency / (Type(static_cast<int>(timAdapter->GetClockDivision())) * targetFrequency)) - 1;
	    } else {
	        for (prescaler = 0; prescaler <= 0xFFFF; ++prescaler) {
	            uint32 tmpPeriod = static_cast<uint32>(sourceFrequency / (Type(static_cast<int>(timAdapter->GetClockDivision())) * Type(static_cast<int>(prescaler + 1)) * targetFrequency)) - 1;
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
	    	timAdapter->GetBitness() == ATIM::Bitness::B32 ? period : static_cast<uint32>(static_cast<uint16>(period))
	    };
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
