#pragma once
#include <Periphery.h>
#include <Utilities/Status.h>
#include <Utilities/Function.h>


namespace WaitCondition {
	bool Wait(Function<bool(), 32> condition, uint32 timeout);
	bool WaitDouble(Function<bool(), 32> condition, Function<bool(), 32> mandatoryCondition, uint32 timeout);
};
