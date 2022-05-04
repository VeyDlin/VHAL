#pragma once
#include <Periphery.h>
#include <Status.h>
#include <functional>
#include <cassert>
#include "../../TIMAdapter.h"




struct TimerChannel {
	ATIM *timer;
	const TIMAdapter::ChannelMode *channel;
};




class ITIMHelper {
protected:
	TIMAdapter *timAdapter;

	const TIMAdapter::ChannelMode *channel;
	TIMAdapter::ChannelSelect channelSelect;


public:
	ITIMHelper() { }

	ITIMHelper(TIMAdapter *adapter, const TIMAdapter::ChannelMode *channel) {
		timAdapter = adapter;
	}




	void SetState(bool isEnable) {
		timAdapter->SetChannelsState({{ channel, GetChannelEnableSelect(isEnable) }});
	}




private:
	TIMAdapter::ChannelEnableSelect GetChannelEnableSelect(bool isEnable) {
		// TODO: set
		return TIMAdapter::ChannelEnableSelect::EnableAll;
	}





};
















