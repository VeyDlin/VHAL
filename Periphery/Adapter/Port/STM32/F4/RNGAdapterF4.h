#pragma once
#include <Adapter/Port/STM32/Common/RNGSTM32.h>

#if defined(RNG)


using ARNG = class RNGAdapterF4;


// BSP: 48 MHz RNG kernel clock (PLL48CK) and the RNG bus clock enabled. The bus clock bit is RCC_AHB2ENR.RNGEN on
// F405, F407, F412 and the other F4 chips with RNG, and RCC_AHB1ENR.RNGEN on F410 (IsClockEnabled() selects it with
// the RCC_AHB1ENR_RNGEN define). RNG is present on STM32F405/F407/F410/F412/F413/F415/F417/F423/F427/F429/F437/F439/
// F469/F479; F411xE has no RNG and this header compiles empty there. RNG_IRQn is optional, see RNGSTM32.h. Clock tree
// and NVIC are not configured by this adapter. F4 RNG has no clock error detection (CED), so the shared code skips that
// call on this family.
class RNGAdapterF4: public RNGAdapterSTM32 {
public:
	RNGAdapterF4() = default;


	RNGAdapterF4(RNG_TypeDef *rng):RNGAdapterSTM32(rng) {
	}


protected:
	virtual bool IsClockEnabled() const override {
		#if defined(RCC_AHB1ENR_RNGEN)
			return LL_AHB1_GRP1_IsEnabledClock(LL_AHB1_GRP1_PERIPH_RNG) != 0U;
		#else
			return LL_AHB2_GRP1_IsEnabledClock(LL_AHB2_GRP1_PERIPH_RNG) != 0U;
		#endif
	}
};

#endif
