#pragma once
#include <Application.h>



class McuWatchDogTask: public ThreadStatic<128> {
public:
	virtual void Execute() override {
		BSP::mcuWatchDog.SetParameters({ 10000 });
		auto deadline = BSP::mcuWatchDog.GetDeadlineMs() / 2;
		BSP::mcuWatchDog.Start();
		while (true) {
			BSP::mcuWatchDog.Reset();
			Sleep(1ms * deadline);
		}
	}
};
