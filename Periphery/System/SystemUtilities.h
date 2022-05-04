#pragma once
#include "System.h"



namespace SystemUtilities {
	bool WaitCondition(std::function<bool()> condition, uint32 timeout);
	bool WaitConditionDouble(std::function<bool()> condition, std::function<bool()> mandatoryCondition, uint32 timeout);

};
