#pragma once
#include <Adapter/Port/STM32/Common/RTCv3STM32.h>


// STM32G0 RTC. Shared steps for the BSP are in Common/RTCv3STM32.h. Chip-specific parts:
//   - Interrupt vector: RTC_TAMP_IRQn (2) is the only RTC vector on G0. It serves alarm A and B, the wakeup timer and
//     the tamper inputs; call IrqHandler() from it. The CMSIS comment on that vector names EXTI lines 19 and 21.
//   - EXTI lines: line 19 for both the alarms and the wakeup timer, in interrupt mode with rising-edge trigger. The
//     G0 HAL maps RTC_EXTI_LINE_ALARM_EVENT and RTC_EXTI_LINE_WAKEUPTIMER_EVENT to EXTI_IMR1_IM19
//     (stm32g0xx_hal_rtc.h, stm32g0xx_hal_rtc_ex.h). Line 21 is the RTC tamper line, which this port does not use.
//     Confirm line 19 in the EXTI table of RM0444 when bringing up a new G0 chip; the repo has no copy of that manual.
//   - Backup registers: TAMP_BKP0R to TAMP_BKP4R (5 registers). Every G0 CMSIS header in the repo declares TAMP with
//     these five registers: G030, G031, G041, G050, G051, G061, G070, G071, G081, G0B0, G0B1 and G0C1.


using ARTC = class RTCAdapterG0;


class RTCAdapterG0: public RTCv3STM32<LL_EXTI_LINE_19, LL_EXTI_LINE_19, 5> {
public:
	RTCAdapterG0() = default;


	RTCAdapterG0(RTC_TypeDef *rtc, uint32 clockSourceHz) : RTCv3STM32(rtc, clockSourceHz) {
	}
};
