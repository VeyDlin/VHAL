#pragma once
#include <Adapter/Port/STM32/Common/RNGSTM32.h>

#if defined(RNG)


using ARNG = class RNGAdapterG4;


// BSP: 48 MHz RNG kernel clock (HSI48 or PLLQ), RNG bus clock enabled in RCC_AHB2ENR (RNGEN). RNG_IRQn is optional,
// see RNGSTM32.h. Clock tree and NVIC are not configured by this adapter.
class RNGAdapterG4: public RNGAdapterSTM32 {
public:
	RNGAdapterG4() = default;


	RNGAdapterG4(RNG_TypeDef *rng):RNGAdapterSTM32(rng) {
	}


protected:
	virtual bool IsClockEnabled() const override {
		return LL_AHB2_GRP1_IsEnabledClock(LL_AHB2_GRP1_PERIPH_RNG) != 0U;
	}
};

#endif
