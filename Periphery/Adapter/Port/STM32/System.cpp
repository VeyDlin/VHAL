#include <VHAL.h>
#include <System/NestedCriticalSection.h>

#if defined(VHAL_STM32)


void System::InitPlatform() {

}


void System::Reset() {
    NVIC_SystemReset();
}


// The nesting logic is shared with the other ports (System/NestedCriticalSection.h). This file only supplies the PRIMASK
// operations, with the CMSIS intrinsics.
namespace {


struct Stm32Interrupts {
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
        NestedCriticalSection<Stm32Interrupts>::Enter();
    } else {
        NestedCriticalSection<Stm32Interrupts>::Leave();
    }
}


uint32 System::GetCoreTick() {
	#if defined(CoreDebug)
		return DWT->CYCCNT;
	#else
		return 0;
	#endif
}


bool System::InitDelayUs() {
	#if defined(CoreDebug)
		CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
		DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
		DWT->CYCCNT = 0;

		__asm volatile("NOP");
		__asm volatile("NOP");
		__asm volatile("NOP");

		return static_cast<bool>(DWT->CYCCNT);
	#else
		return false;
	#endif
}


void System::DelayUs(uint32 delay) {
#if defined(CoreDebug)
    uint32 maxDelayTicks = 1 + (delay / 1000) + 1;
    uint32 startSysTick = GetTick();
    uint32 startCoreTick = GetCoreTick();
    uint32 targetDelta = delay * (GetCoreClock() / 1000000);

    while ((GetCoreTick() - startCoreTick) < targetDelta) {
    	if ((GetTick() - startSysTick) > maxDelayTicks) {
    		break;
    	}
    }
#else
    DelayMs(delay / 1000);
#endif
}


System::DeviceId System::GetDeviceId() {
	return DeviceId {
		.words = {
			LL_GetUID_Word0(),
			LL_GetUID_Word1(),
			LL_GetUID_Word2()
		}
	};
}


bool System::IsInterrupt() {
	return __get_IPSR() != 0;
}

#endif
