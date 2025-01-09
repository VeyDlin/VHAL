#pragma once
#include <System/System.h>
#include <functional>


namespace WaitCondition {
	bool Wait(std::function<bool()> condition, uint32 timeout);
	bool WaitDouble(std::function<bool()> condition, std::function<bool()> mandatoryCondition, uint32 timeout);
};
