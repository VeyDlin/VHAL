#pragma once
#include <Adapter/Port/STM32/Common/RNGSTM32.h>

#if defined(RNG)


using ARNG = class RNGAdapterG0;


// BSP: 48 MHz RNG kernel clock (HSI48), RNG bus clock enabled in RCC_AHBENR (RNGEN). RNG is present on STM32G041,
// G061, G081 and G0C1 only; G0B1 and G071 have no RNG and this header compiles empty there. Use G041 or G0C1 as the
// validation target. The RNG shares its interrupt vector with AES on G0 (AES_RNG_IRQn); forward it to IrqHandler()
// only if you wire it, see RNGSTM32.h. Clock tree and NVIC are not configured by this adapter.
class RNGAdapterG0: public RNGAdapterSTM32 {
public:
	RNGAdapterG0() = default;


	RNGAdapterG0(RNG_TypeDef *rng):RNGAdapterSTM32(rng) {
	}


protected:
	virtual bool IsClockEnabled() const override {
		return LL_AHB1_GRP1_IsEnabledClock(LL_AHB1_GRP1_PERIPH_RNG) != 0U;
	}
};

#endif
