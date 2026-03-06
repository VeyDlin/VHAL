#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_GPTIMER_ADAPTER


class GPTimerAdapter : public IAdapter {
public:
	enum class CountDirection { Up, Down };

	struct Parameters {
		uint32 resolutionHz = 1000000;
		uint64 alarmCount = 0;
		bool autoReload = true;
		CountDirection direction = CountDirection::Up;
	};


public:
	std::function<void()> onAlarm;


public:
	GPTimerAdapter() = default;

	virtual Status::statusType SetParameters(Parameters val) = 0;
	virtual Status::statusType Start() = 0;
	virtual Status::statusType Stop() = 0;
	virtual Status::statusType SetAlarm(uint64 alarm, bool reload) = 0;
	virtual Status::statusType SetCount(uint64 count) = 0;
	virtual uint64 GetCount() = 0;


protected:
	Parameters parameters;
};
