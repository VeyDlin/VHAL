#pragma once
#include <Utilities/DataTypes.h>


// The nesting critical section every port uses. Sections nest: only the outermost leave restores the interrupt state that the
// outermost enter found. Code that calls a locking helper from inside its own section stays protected, and a section entered
// with interrupts already masked leaves them masked. The depth is global: while it is above zero the interrupts are masked, so
// neither an interrupt nor a context switch can run another section in between.
//
// TInterrupts is the port's interrupt control, with three static operations:
//   uint32 ReadMask()   the interrupt mask as it is now, zero when the interrupts are enabled
//   void Disable()      masks the interrupts
//   void Enable()       unmasks the interrupts
// The port calls Enter() and Leave() from System::CriticalSection(). Every helper here is inline and runs no call on the
// path beyond the port's own operations.
template<typename TInterrupts>
class NestedCriticalSection {
private:
	static volatile uint32 depth;
	static volatile uint32 savedMask;


public:
	// Masks the interrupts and counts one more open section. The outermost enter saves the mask it found.
	static void Enter() {
		uint32 mask = TInterrupts::ReadMask();
		TInterrupts::Disable();
		if (depth == 0) {
			savedMask = mask;
		}

		depth = depth + 1;
	}


	// Counts one section out. Only the outermost leave unmasks, and only when the outermost enter found the interrupts enabled.
	// A leave with no open section unmasks, as the ports always have.
	static void Leave() {
		if (depth == 0) {
			TInterrupts::Enable();
			return;
		}

		depth = depth - 1;
		if (depth == 0 && savedMask == 0) {
			TInterrupts::Enable();
		}
	}
};


template<typename TInterrupts>
volatile uint32 NestedCriticalSection<TInterrupts>::depth = 0;


template<typename TInterrupts>
volatile uint32 NestedCriticalSection<TInterrupts>::savedMask = 0;
