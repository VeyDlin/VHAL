#pragma once
#include <Application.h>


class McuWatchDogTask : public ThreadStatic<2048> {
public:
	void Execute() override {
		BSP::mcuWatchDog.SetParameters({ 10000 });
		const uint32 deadline = BSP::mcuWatchDog.GetDeadlineMs() / 2;
		BSP::mcuWatchDog.Start();

		while (true) {
			BSP::mcuWatchDog.Reset();
			Sleep(1ms * deadline);
		}
	}
};
