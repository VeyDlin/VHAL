#include "SystemUtilities.h"



namespace SystemUtilities {

	//bool (*condition)()
	bool WaitCondition(std::function<bool()> condition, uint32 timeout) {
		uint32 endtime = System::GetTick() + timeout;
		while (!condition()) {
			if (System::GetTick() < endtime) {
				return false;
			}
		}
		return true;
	}



	bool WaitConditionDouble(std::function<bool()> condition, std::function<bool()> mandatoryCondition, uint32 timeout) {
		uint32 endtime = System::GetTick() + timeout;
		while (!condition()) {
			if(!mandatoryCondition()) {
				return false;
			}

			if (System::GetTick() < endtime) {
				return false;
			}
		}
		return true;
	}


};
