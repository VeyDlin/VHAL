#include "WaitCondition.h"
#include <System/System.h>


namespace WaitCondition {

	//bool (*condition)()
	bool Wait(Function<bool()> condition, uint32 timeout) {
		uint32 endtime = System::GetTick() + timeout;
		while (!condition()) {
			if (System::GetTick() < endtime) {
				return false;
			}
		}
		return true;
	}



	bool WaitDouble(Function<bool()> condition, Function<bool()> mandatoryCondition, uint32 timeout) {
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
