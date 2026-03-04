#include <VHAL.h>

#if defined(VHAL_STM32)

uint32 System::GetCoreTick() {
	#if defined(CoreDebug)
		return DWT->CYCCNT;
	#else
		return 0;
	#endif
}


bool System::InitDelayDWT() {
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

#endif
