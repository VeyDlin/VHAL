#pragma once
#include <Periphery.h>
#include <Utilities/Status.h>
#include <functional>
#include "../../TIMAdapter.h"




struct TimerChannel {
	ATIM *timer;
	const ATIM::ChannelMode *channel;
};




class ITIMHelper {
protected:
	ATIM *timAdapter;
	const ATIM::ChannelMode *timChannel;


public:
	ITIMHelper() { }
	ITIMHelper(ATIM& adapter, const ATIM::ChannelMode* channel) : timAdapter(&adapter), timChannel(channel)  { }
	ITIMHelper(TimerChannel& timerChannel) : timAdapter(timerChannel.timer), timChannel(timerChannel.channel)  { }


	ITIMHelper& SetState(bool isEnable) {
		timAdapter->SetChannelsState({{ timChannel, GetChannelEnableSelect(isEnable) }});
		return *this;
	}


	inline void SetDivision(const ATIM::ClockDivisionMode *division) {
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


	inline ATIM* GetAdapter() const {
        return timAdapter;
    }


	inline const ATIM::ChannelMode* GetChannel() const {
        return timChannel;
    }


	inline uint8 GetChannelNumber() {
        return timAdapter->GetChannelIndex(timChannel) + 1;
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
















