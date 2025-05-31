#include "WaitCondition.h"
#include <System/System.h>


namespace WaitCondition {

	//bool (*condition)()
	bool Wait(std::function<bool()> condition, uint32 timeout) {
		uint32 endtime = System::GetTick() + timeout;
		while (!condition()) {
			if (System::GetTick() < endtime) {
				return false;
			}
		}
		return true;
	}



	bool WaitDouble(std::function<bool()> condition, std::function<bool()> mandatoryCondition, uint32 timeout) {
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
