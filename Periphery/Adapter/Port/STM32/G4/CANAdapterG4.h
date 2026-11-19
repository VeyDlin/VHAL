#pragma once
#include <Adapter/Port/STM32/Common/FDCANSTM32.h>


// FDCAN on STM32G4 (G431, G441, G471, G473, G474, G483, G484 and the chips with FDCAN1 only). Classic CAN and CAN FD.
// Instances: FDCAN1 always, FDCAN2 and FDCAN3 where the device header defines them. The message RAM of each instance is a
// fixed block at SRAMCAN_BASE + instance x 0x350, see FDCANSTM32.h.
//
// What the BSP must do, in addition to the list in FDCANSTM32.h:
//   - Clock: set RCC_APB1ENR1.FDCANEN before SetParameters(). The bit covers all FDCAN instances of the chip.
//   - Kernel clock: select the source with RCC_CCIPR.FDCANSEL (HSE, PLL "Q" output or PCLK1). The clock is given to the
//     constructor after the CKDIV divider of FDCAN_CONFIG, which the BSP sets.
//   - NVIC: FDCAN1_IT0_IRQn and FDCAN1_IT1_IRQn, and FDCAN2_IT0_IRQn, FDCAN2_IT1_IRQn, FDCAN3_IT0_IRQn, FDCAN3_IT1_IRQn on
//     chips that have the instance. Call IrqHandler() of the instance from each of them.
//
// Filter support: 28 standard and 8 extended filters per instance, classic id/mask only.
#if defined(FDCAN1)

using ACAN = class CANAdapterG4;


class CANAdapterG4: public FDCANAdapterSTM32 {
public:
	CANAdapterG4() = default;


	CANAdapterG4(FDCAN_GlobalTypeDef *can, uint32 kernelClockHz): FDCANAdapterSTM32(can, kernelClockHz) {
	}


protected:
	virtual bool IsClockEnabled() const override {
		return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_FDCAN) != 0U;
	}
};

#endif
