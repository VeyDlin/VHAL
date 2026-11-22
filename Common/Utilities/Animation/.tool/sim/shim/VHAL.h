#pragma once
// Host stand-in for the RTOS part of the VHAL umbrella header. AnimationRTOS needs a timer thread, the system clock and a
// sleep. The shim keeps the last created thread, so a test runs its timer by hand, and the clock is a variable the test sets.
#include <Utilities/DataTypes.h>
#include <chrono>
#include <cstddef>


// The real umbrella header makes the duration literals visible to the RTOS code, so the shim does too.
using namespace std::chrono_literals;


namespace OS {


enum class ThreadPriority {
	low,
	normal,
	high
};


class RunnableBase {
public:
	virtual void Execute() = 0;
	virtual ~RunnableBase() = default;
};


template<std::size_t stackSize>
class Timer : public RunnableBase {
public:
	std::chrono::milliseconds interval{0};
	bool autoReload = false;
	bool running = false;


	void Start() {
		running = true;
	}


	void Stop() {
		running = false;
	}
};


struct RTOS {
	static inline RunnableBase* created = nullptr;


	template<typename ThreadType>
	static void CreateThread(ThreadType& thread, ThreadPriority) {
		created = &thread;
	}
};


struct IThread {
	static void Sleep(std::chrono::milliseconds) {
	}
};


}


namespace System {


inline uint64 clockMs = 0;


inline uint64 GetMs() {
	return clockMs;
}


}
