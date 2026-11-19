#pragma once
#include <Adapter/Port/STM32/Common/FDCANSTM32.h>


// FDCAN on STM32G0 with one instance (FDCAN1), on STM32G0B1 and G0C1. Classic CAN and CAN FD. STM32G071 has no FDCAN, so
// this header is empty there. The message RAM is a fixed block at SRAMCAN_BASE, see FDCANSTM32.h.
//
// What the BSP must do, in addition to the list in FDCANSTM32.h:
//   - Clock: set RCC_APBENR1.FDCANEN before SetParameters().
//   - Kernel clock: select the source with RCC_CCIPR.FDCANSEL. The clock is given to the constructor after the CKDIV
//     divider of FDCAN_CONFIG, which the BSP sets.
//   - NVIC: the FDCAN lines are shared with timers. TIM16_FDCAN_IT0_IRQn carries TIM16 and FDCAN1_IT0, and
//     TIM17_FDCAN_IT1_IRQn carries TIM17 and FDCAN1_IT1. The handler of each vector calls IrqHandler() of the CAN adapter
//     and the timer code. The adapter does not check which source fired, so it is safe to call from either.
#if defined(FDCAN1)

using ACAN = class CANAdapterG0;


class CANAdapterG0: public FDCANAdapterSTM32 {
public:
	CANAdapterG0() = default;


	CANAdapterG0(FDCAN_GlobalTypeDef *can, uint32 kernelClockHz): FDCANAdapterSTM32(can, kernelClockHz) {
	}


protected:
	virtual bool IsClockEnabled() const override {
		return LL_APB1_GRP1_IsEnabledClock(LL_APB1_GRP1_PERIPH_FDCAN) != 0U;
	}
};

#endif
