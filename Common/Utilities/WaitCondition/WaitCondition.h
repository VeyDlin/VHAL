#pragma once
#include <System/System.h>

namespace WaitCondition {


// Polls without giving up the processor. Use it for a flag that a peripheral sets within microseconds.
struct NoYield {
	static inline void Pause() {
	}
};


// Gives the processor to the RTOS for one millisecond between polls, through System::rtosDelayMsHandle. With no handle set it
// polls like NoYield. Use it for a wait that can take milliseconds.
struct RtosYield {
	static inline void Pause() {
		if (System::rtosDelayMsHandle) {
			System::rtosDelayMsHandle(1);
		}
	}
};


// Returns ok as soon as the predicate is true, or timeout once timeoutMs ticks have passed while it is still false. A tick is one
// period of System::TickHandler: the application calls it once per millisecond, so there a tick is a millisecond. The predicate
// and the policy are template arguments, so a lambda inlines, and nothing is allocated or wrapped in std::function.
//
// The predicate runs before the clock is read, so a condition that is already true costs one call. The result is timeout only
// when the last predicate call returned false. A condition that is true at that call returns ok, even when the call reaches the
// limit.
//
// The tick must move while the wait runs. Inside a critical section or with the interrupts masked it stops, and the wait cannot
// time out. Never wait there.
template<typename Policy = NoYield, typename Predicate>
inline ResultStatus WaitFor(Predicate&& predicate, uint32 timeoutMs) {
	if (predicate()) {
		return ResultStatus::ok;
	}

	uint32 start = System::GetTick32();
	while (true) {
		Policy::Pause();
		if (predicate()) {
			return ResultStatus::ok;
		}

		// Unsigned subtraction keeps the elapsed time right across the 32-bit wrap.
		if (System::GetTick32() - start >= timeoutMs) {
			return ResultStatus::timeout;
		}
	}
}


}
