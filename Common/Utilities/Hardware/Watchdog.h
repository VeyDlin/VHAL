#include <System/System.h>
#include <Adapter/OSAdapter/RTOS.h>
#include <Adapter/OSAdapter/Timer.h>
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
		if(onEnd != nullptr) {
			onEnd();
			this->autoReload = false;
		}
	}
};