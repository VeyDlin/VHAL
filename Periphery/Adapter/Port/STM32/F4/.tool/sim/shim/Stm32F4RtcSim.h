#pragma once
// Register model of the F4 RTC and of the EXTI pending bits of lines 17 and 22, for the host tests. The IrqHandler path reads
// and clears the alarm and wakeup flags (RTC_ISR), reads the interrupt enables (RTC_CR), and clears the EXTI pending bits.
// Those are modelled with the RM0090 bit positions. The rest of the LL surface that RTCAdapterF4.h reaches is declared so the
// class compiles; those calls return 0 and do nothing, and no test relies on them. The flag readers are plain functions because
// the adapter passes them as function pointers.
#include <System/System.h>


// RTC_ISR bits.
#define RTC_ISR_ALRAWF      (1U << 0)
#define RTC_ISR_ALRBWF      (1U << 1)
#define RTC_ISR_WUTWF       (1U << 2)
#define RTC_ISR_RSF         (1U << 5)
#define RTC_ISR_INITF       (1U << 6)
#define RTC_ISR_INITS       (1U << 4)
#define RTC_ISR_RECALPF     (1U << 16)
#define RTC_ISR_ALRAF       (1U << 8)
#define RTC_ISR_ALRBF       (1U << 9)
#define RTC_ISR_WUTF        (1U << 10)

// RTC_CR interrupt enables.
#define RTC_CR_ALRAIE       (1U << 12)
#define RTC_CR_ALRBIE       (1U << 13)
#define RTC_CR_WUTIE        (1U << 14)


struct RTC_TypeDef {
	volatile uint32 ISR = 0;
	volatile uint32 CR = 0;
	volatile uint32 CALR = 0;
};


#define WRITE_REG(REG, VAL) ((REG) = (VAL))


// The port's register guards enter a critical section; the host has no interrupts to mask.
inline void System::CriticalSection(bool) { }


// The EXTI pending bits of the two RTC lines. Pending is set by a test; the adapter clears it.
inline uint32 simExtiPending = 0;

#define LL_EXTI_LINE_17 (1U << 17)
#define LL_EXTI_LINE_22 (1U << 22)

inline void LL_EXTI_ClearFlag_0_31(uint32 line) { simExtiPending &= ~line; }


// Clock and backup-domain calls: the clock is on and the backup access is on, so the port's guards pass.
#define LL_APB1_GRP1_PERIPH_PWR (1U << 28)
inline uint32 LL_APB1_GRP1_IsEnabledClock(uint32) { return 1; }
inline void LL_PWR_EnableBkUpAccess() { }
inline void LL_PWR_DisableBkUpAccess() { }
inline uint32 LL_PWR_IsEnabledBkUpAccess() { return 1; }

#define LL_RCC_RTC_CLKSOURCE_NONE 0U
inline uint32 LL_RCC_GetRTCClockSource() { return LL_RCC_RTC_CLKSOURCE_NONE; }
inline uint32 LL_RCC_IsEnabledRTC() { return 0; }


// Flag readers and interrupt enables: these read the model.
inline uint32 LL_RTC_IsActiveFlag_ALRA(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_ALRAF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_ALRB(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_ALRBF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_WUT(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_WUTF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_RS(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_RSF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_INIT(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_INITF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_INITS(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_INITS) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_ALRAW(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_ALRAWF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_ALRBW(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_ALRBWF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_WUTW(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_WUTWF) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsActiveFlag_RECALP(RTC_TypeDef *h) { return (h->ISR & RTC_ISR_RECALPF) != 0 ? 1u : 0u; }

inline void LL_RTC_ClearFlag_ALRA(RTC_TypeDef *h) { h->ISR &= ~RTC_ISR_ALRAF; }
inline void LL_RTC_ClearFlag_ALRB(RTC_TypeDef *h) { h->ISR &= ~RTC_ISR_ALRBF; }
inline void LL_RTC_ClearFlag_WUT(RTC_TypeDef *h) { h->ISR &= ~RTC_ISR_WUTF; }
inline void LL_RTC_ClearFlag_RS(RTC_TypeDef *h) { h->ISR &= ~RTC_ISR_RSF; }

inline uint32 LL_RTC_IsEnabledIT_ALRA(RTC_TypeDef *h) { return (h->CR & RTC_CR_ALRAIE) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsEnabledIT_ALRB(RTC_TypeDef *h) { return (h->CR & RTC_CR_ALRBIE) != 0 ? 1u : 0u; }
inline uint32 LL_RTC_IsEnabledIT_WUT(RTC_TypeDef *h) { return (h->CR & RTC_CR_WUTIE) != 0 ? 1u : 0u; }

inline void LL_RTC_EnableIT_ALRA(RTC_TypeDef *h) { h->CR |= RTC_CR_ALRAIE; }
inline void LL_RTC_EnableIT_ALRB(RTC_TypeDef *h) { h->CR |= RTC_CR_ALRBIE; }
inline void LL_RTC_EnableIT_WUT(RTC_TypeDef *h) { h->CR |= RTC_CR_WUTIE; }
inline void LL_RTC_DisableIT_ALRA(RTC_TypeDef *h) { h->CR &= ~RTC_CR_ALRAIE; }
inline void LL_RTC_DisableIT_ALRB(RTC_TypeDef *h) { h->CR &= ~RTC_CR_ALRBIE; }
inline void LL_RTC_DisableIT_WUT(RTC_TypeDef *h) { h->CR &= ~RTC_CR_WUTIE; }


// Calendar, alarm, wakeup and calibration calls. The IrqHandler path does not reach them; they are declared for the class.
#define LL_RTC_STUB(NAME) \
	template<typename... Args> \
	inline uint32 NAME(Args...) { return 0; }

LL_RTC_STUB(LL_RTC_ALMA_ConfigTime)
LL_RTC_STUB(LL_RTC_ALMA_Disable)
LL_RTC_STUB(LL_RTC_ALMA_DisableWeekday)
LL_RTC_STUB(LL_RTC_ALMA_Enable)
LL_RTC_STUB(LL_RTC_ALMA_EnableWeekday)
LL_RTC_STUB(LL_RTC_ALMA_SetDay)
LL_RTC_STUB(LL_RTC_ALMA_SetMask)
LL_RTC_STUB(LL_RTC_ALMA_SetWeekDay)
LL_RTC_STUB(LL_RTC_ALMB_ConfigTime)
LL_RTC_STUB(LL_RTC_ALMB_Disable)
LL_RTC_STUB(LL_RTC_ALMB_DisableWeekday)
LL_RTC_STUB(LL_RTC_ALMB_Enable)
LL_RTC_STUB(LL_RTC_ALMB_EnableWeekday)
LL_RTC_STUB(LL_RTC_ALMB_SetDay)
LL_RTC_STUB(LL_RTC_ALMB_SetMask)
LL_RTC_STUB(LL_RTC_ALMB_SetWeekDay)
LL_RTC_STUB(LL_RTC_BAK_GetRegister)
LL_RTC_STUB(LL_RTC_BAK_SetRegister)
LL_RTC_STUB(LL_RTC_DATE_Config)
LL_RTC_STUB(LL_RTC_DATE_GetDay)
LL_RTC_STUB(LL_RTC_DATE_GetMonth)
LL_RTC_STUB(LL_RTC_DATE_GetWeekDay)
LL_RTC_STUB(LL_RTC_DATE_GetYear)
LL_RTC_STUB(LL_RTC_DisableInitMode)
LL_RTC_STUB(LL_RTC_DisableWriteProtection)
LL_RTC_STUB(LL_RTC_EnableInitMode)
LL_RTC_STUB(LL_RTC_EnableWriteProtection)
LL_RTC_STUB(LL_RTC_GetSynchPrescaler)
LL_RTC_STUB(LL_RTC_SetAsynchPrescaler)
LL_RTC_STUB(LL_RTC_SetSynchPrescaler)
LL_RTC_STUB(LL_RTC_TIME_Config)
LL_RTC_STUB(LL_RTC_TIME_GetHour)
LL_RTC_STUB(LL_RTC_TIME_GetMinute)
LL_RTC_STUB(LL_RTC_TIME_GetSecond)
LL_RTC_STUB(LL_RTC_TIME_GetSubSecond)
LL_RTC_STUB(LL_RTC_WAKEUP_Disable)
LL_RTC_STUB(LL_RTC_WAKEUP_Enable)
LL_RTC_STUB(LL_RTC_WAKEUP_SetAutoReload)
LL_RTC_STUB(LL_RTC_WAKEUP_SetClock)

#undef LL_RTC_STUB


// Constants of the calls above. Each has its own value, so no two cases of a switch in the port collide.
#define LL_RTC_ALMA_MASK_NONE           0x0100U
#define LL_RTC_ALMA_MASK_DATEWEEKDAY    0x0101U
#define LL_RTC_ALMA_MASK_HOURS          0x0102U
#define LL_RTC_ALMA_MASK_MINUTES        0x0103U
#define LL_RTC_ALMA_MASK_SECONDS        0x0104U
#define LL_RTC_ALMA_TIME_FORMAT_AM      0x0105U
#define LL_RTC_CALIB_INSERTPULSE_NONE   0x0106U
#define LL_RTC_CALIB_INSERTPULSE_SET    0x0107U
#define LL_RTC_CALIB_PERIOD_32SEC       0x0108U
#define LL_RTC_HOURFORMAT_24HOUR        0x0109U
#define LL_RTC_WAKEUPCLOCK_CKSPRE       0x010AU
#define LL_RTC_WAKEUPCLOCK_CKSPRE_WUT   0x010BU
#define LL_RTC_WAKEUPCLOCK_DIV_16       0x010CU
#define LL_RTC_WAKEUPCLOCK_DIV_2        0x010DU
#define LL_RTC_WAKEUPCLOCK_DIV_4        0x010EU
#define LL_RTC_WAKEUPCLOCK_DIV_8        0x010FU
