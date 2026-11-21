#pragma once
#include <System/System.h>
#include <atomic>
#include <type_traits>


// One-bit latch. Set() can run in an interrupt, and Take() in a task. Take() reads and clears the latch in one step, so a Set that
// arrives during the take stays for the next Take. Two sets before a take make one event. The latch has one owner, so it cannot
// be copied.
//
// lockFree picks the storage. Where bool atomics are always lock-free, the latch is a std::atomic<bool>, and Set() is safe in any
// interrupt. Elsewhere, such as Cortex-M0+ with no exclusive load and store, each operation runs in a System critical section. That
// is safe in an interrupt only where the port's critical section is: on ESP32 it is task-level, so call Set() from a task there.
template<bool lockFree>
class BasicTrigger {
private:
	using Flag = std::conditional_t<lockFree, std::atomic<bool>, volatile bool>;

	Flag flag{false};


public:
	BasicTrigger() = default;
	BasicTrigger(const BasicTrigger&) = delete;
	BasicTrigger& operator=(const BasicTrigger&) = delete;


	void Set() {
		if constexpr (lockFree) {
			flag.store(true, std::memory_order_release);
		} else {
			System::CriticalSection(true);
			flag = true;
			System::CriticalSection(false);
		}
	}


	// Returns true when the latch is set, and clears it.
	[[nodiscard]] bool Take() {
		if constexpr (lockFree) {
			return flag.exchange(false, std::memory_order_acq_rel);
		} else {
			System::CriticalSection(true);
			bool value = flag;
			flag = false;
			System::CriticalSection(false);
			return value;
		}
	}


	// Reads the latch without clearing it.
	bool IsSet() const {
		if constexpr (lockFree) {
			return flag.load(std::memory_order_acquire);
		} else {
			return flag;
		}
	}
};


using Trigger = BasicTrigger<std::atomic<bool>::is_always_lock_free>;
