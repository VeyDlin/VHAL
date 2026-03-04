#pragma once
#define VHAL_RTOS
#define VHAL_RTOS_TIMER
#include <VHAL.h>
#include <functional>


template<std::size_t stackSize>
class Watchdog: public Timer<stackSize> {
public:
	std::function<void()> onEnd;

public:
	Watchdog() {
		RTOS::CreateThread(*this);
	}
	
	virtual void Execute() override {
		if(onEnd) {
			onEnd();
			this->autoReload = false;
		}
	}
};
