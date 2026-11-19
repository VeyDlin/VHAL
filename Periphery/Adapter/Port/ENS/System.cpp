#include <VHAL.h>
#include <System/NestedCriticalSection.h>

#if defined(VHAL_ENS)


void System::InitPlatform() {

}


void System::Reset() {
    NVIC_SystemReset();
}


// The nesting logic is shared with the other ports (System/NestedCriticalSection.h). This file only supplies the PRIMASK
// operations, with the CMSIS intrinsics.
namespace {


struct EnsInterrupts {
    static uint32 ReadMask() {
        return __get_PRIMASK();
    }


    static void Disable() {
        __disable_irq();
    }


    static void Enable() {
        __enable_irq();
    }
};


}


void System::CriticalSection(bool isEnable) {
    if (isEnable) {
        NestedCriticalSection<EnsInterrupts>::Enter();
    } else {
        NestedCriticalSection<EnsInterrupts>::Leave();
    }
}


bool System::InitDelayUs() {
	// Cortex-M0 does not have DWT
	return false;
}


void System::DelayUs(uint32 delay) {
	// Loop-based microsecond delay for Cortex-M0
	// Approximate: each iteration ~4 cycles at SystemCoreClock
	uint32 cycles = delay * (SystemCoreClock / 1000000) / 4;
	while (cycles--) {
		__NOP();
	}
}


uint32 System::GetCoreTick() {
	// Cortex-M0 has no DWT cycle counter, use SysTick current value
	return SysTick->VAL;
}


System::DeviceId System::GetDeviceId() {
	// Read from MTP info block (ENS001-specific)
	// MTP info block at address 0x00080000 contains device unique ID
	DeviceId id = {};
	volatile uint32 *mtpInfo = reinterpret_cast<volatile uint32*>(0x00080000);
	id.words[0] = mtpInfo[0];
	id.words[1] = mtpInfo[1];
	id.words[2] = mtpInfo[2];
	return id;
}


bool System::IsInterrupt() {
	return __get_IPSR() != 0;
}

#endif
