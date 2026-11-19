#pragma once
#include <Adapter/Port/STM32/Common/RTCv3STM32.h>


// STM32G4 RTC. Shared steps for the BSP are in Common/RTCv3STM32.h. Chip-specific parts:
//   - Interrupt vectors: RTC_Alarm_IRQn (41) serves alarm A and B; RTC_WKUP_IRQn (3) serves the wakeup timer. Call
//     IrqHandler() from both. RTC_TAMP_LSECSS_IRQn (2) is not used by this port.
//   - EXTI lines: line 17 (RTC alarms, interrupt mode, rising edge) and line 20 (RTC wakeup, interrupt mode, rising
//     edge).
//   - Backup registers: TAMP_BKP0R to TAMP_BKP15R on G431, G441 and G411 (16 registers, as the CMSIS header declares);
//     TAMP_BKP0R to TAMP_BKP31R on the other G4 chips (32 registers).


// Returns the backup register count the CMSIS header of the selected G4 chip declares.
static constexpr uint8 rtcBackupRegisterCountG4() {
#if defined(STM32G431xx) || defined(STM32G441xx) || defined(STM32G411xB) || defined(STM32G411xC)
	return 16;
#else
	return 32;
#endif
}


using ARTC = class RTCAdapterG4;


class RTCAdapterG4: public RTCv3STM32<LL_EXTI_LINE_17, LL_EXTI_LINE_20, rtcBackupRegisterCountG4()> {
public:
	RTCAdapterG4() = default;


	RTCAdapterG4(RTC_TypeDef *rtc, uint32 clockSourceHz) : RTCv3STM32(rtc, clockSourceHz) {
	}
};
