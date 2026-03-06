#pragma once
#define VHAL_RTOS
#define VHAL_RTOS_TIMER
#include <VHAL.h>
#include "Animation.h"


template<typename T, std::size_t stackSize>
class AnimationRTOS : public Animation<T> {
private:
	class AnimationTimer : public OS::Timer<stackSize> {
	public:
		AnimationRTOS* owner;

		virtual void Execute() override {
			if (owner && owner->IsActive()) {
				owner->Update(System::GetMs());
			}
		}
	};

	AnimationTimer timer;


public:
	AnimationRTOS(OS::ThreadPriority prior = OS::ThreadPriority::normal) {
		timer.interval = 17ms;
		timer.autoReload = true;
		timer.owner = this;
		OS::RTOS::CreateThread(timer, prior);
	}


	AnimationRTOS& SetFps(uint8_t val) {
		timer.interval = std::chrono::milliseconds(1000 / val);
		return *this;
	}


	AnimationRTOS& SetInterval(std::chrono::milliseconds interval) {
		timer.interval = interval;
		return *this;
	}


	std::chrono::milliseconds GetInterval() {
		return timer.interval;
	}


	void WaitEnd() {
		while (this->IsActive()) {
			OS::IThread::Sleep(1ms);
		}
	}
};
