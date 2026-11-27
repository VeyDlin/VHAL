#pragma once
// Host stand-in for the RTOS part of the VHAL umbrella header. The climate controls run their loop in Execute, and each
// loop iteration starts with Sleep. The shim calls a test hook from Sleep and stops the loop by throwing after the tick
// budget is spent, so a test runs a fixed number of iterations.
#include <Utilities/DataTypes.h>
#include <chrono>
#include <cstddef>
#include <functional>


// The real umbrella header makes the duration literals visible to the controls, so the shim does too.
using namespace std::chrono_literals;


struct ClimateTickStop {
};


namespace ClimateShim {


inline uint32 tickBudget = 0;
inline std::function<void()> tickHook;


}


template<std::size_t stackSize>
class Thread {
public:
	virtual void Execute() = 0;
	virtual ~Thread() = default;


	void Sleep(std::chrono::milliseconds) {
		if (ClimateShim::tickHook) {
			ClimateShim::tickHook();
		}
		if (ClimateShim::tickBudget == 0) {
			throw ClimateTickStop{};
		}
		ClimateShim::tickBudget--;
	}
};


struct RTOS {
	template<typename ThreadType>
	static void CreateThread(ThreadType&) {
	}
};
