#pragma once
// Register model of the F4 timers for the host tests. It has the LL functions, constants and register bits that
// TIMAdapterF4.h reaches, and nothing else. The bit positions are the CMSIS ones, and the LL constants keep the F4 encoding
// of the input-capture fields: the active input, prescaler and filter values sit 16 bits up, as the header defines them.
// Flags follow the device rule: a flag is cleared by writing 0 to it, so a clear keeps every other flag. LL_TIM_SetRemap
// stores the value it is given; the remap fields of OR are not split here.
#include <System/System.h>


// CR1, EGR, BDTR and SMCR bits.
#define TIM_CR1_CEN          (1U << 0)
#define TIM_CR1_ARPE         (1U << 7)
#define TIM_CR1_CKD          (3U << 8)
#define TIM_EGR_UG           (1U << 0)
#define TIM_BDTR_MOE         (1U << 15)
#define TIM_SMCR_MSM         (1U << 7)

// DIER interrupt enables.
#define TIM_DIER_UIE         (1U << 0)
#define TIM_DIER_CC1IE       (1U << 1)
#define TIM_DIER_CC2IE       (1U << 2)
#define TIM_DIER_CC3IE       (1U << 3)
#define TIM_DIER_CC4IE       (1U << 4)
#define TIM_DIER_COMIE       (1U << 5)
#define TIM_DIER_TIE         (1U << 6)
#define TIM_DIER_BIE         (1U << 7)

// SR flags.
#define TIM_SR_UIF           (1U << 0)
#define TIM_SR_CC1IF         (1U << 1)
#define TIM_SR_CC2IF         (1U << 2)
#define TIM_SR_CC3IF         (1U << 3)
#define TIM_SR_CC4IF         (1U << 4)
#define TIM_SR_COMIF         (1U << 5)
#define TIM_SR_TIF           (1U << 6)
#define TIM_SR_BIF           (1U << 7)

// CCMR1 holds channels 1 and 2, CCMR2 holds channels 3 and 4. Each channel has one byte: the select, prescaler and filter of
// an input, or the mode of an output.
#define TIM_CCMR1_CC1S       (3U << 0)
#define TIM_CCMR1_CC1S_0     (1U << 0)
#define TIM_CCMR1_CC1S_1     (2U << 0)
#define TIM_CCMR1_CC2S       (3U << 8)
#define TIM_CCMR2_CC3S       (3U << 0)
#define TIM_CCMR2_CC4S       (3U << 8)
#define TIM_CCMR1_IC1PSC     (3U << 2)
#define TIM_CCMR1_IC1PSC_0   (1U << 2)
#define TIM_CCMR1_IC1PSC_1   (2U << 2)
#define TIM_CCMR1_IC1F       (15U << 4)
#define TIM_CCMR1_IC1F_0     (1U << 4)
#define TIM_CCMR1_IC1F_1     (2U << 4)
#define TIM_CCMR1_IC1F_2     (4U << 4)
#define TIM_CCMR1_IC1F_3     (8U << 4)
#define TIM_CCMR1_IC2PSC     (3U << 10)
#define TIM_CCMR1_IC2F       (15U << 12)
#define TIM_CCMR2_IC3PSC     (3U << 2)
#define TIM_CCMR2_IC3F       (15U << 4)
#define TIM_CCMR2_IC4PSC     (3U << 10)
#define TIM_CCMR2_IC4F       (15U << 12)

// CCER enables and polarities. The N bits take the values of the vendor LL header. Whether the general purpose timers
// implement CC1NP for both-edge capture is not checked against RM0090 here.
#define TIM_CCER_CC1E        (1U << 0)
#define TIM_CCER_CC1P        (1U << 1)
#define TIM_CCER_CC1NE       (1U << 2)
#define TIM_CCER_CC1NP       (1U << 3)
#define TIM_CCER_CC2E        (1U << 4)
#define TIM_CCER_CC2P        (1U << 5)
#define TIM_CCER_CC2NE       (1U << 6)
#define TIM_CCER_CC2NP       (1U << 7)
#define TIM_CCER_CC3E        (1U << 8)
#define TIM_CCER_CC3P        (1U << 9)
#define TIM_CCER_CC3NE       (1U << 10)
#define TIM_CCER_CC3NP       (1U << 11)
#define TIM_CCER_CC4E        (1U << 12)
#define TIM_CCER_CC4P        (1U << 13)
#define TIM_CCER_CC4NP       (1U << 15)


// LL constants, with the values of the F4 LL header.
#define LL_TIM_CHANNEL_CH1                     TIM_CCER_CC1E
#define LL_TIM_CHANNEL_CH1N                    TIM_CCER_CC1NE
#define LL_TIM_CHANNEL_CH2                     TIM_CCER_CC2E
#define LL_TIM_CHANNEL_CH2N                    TIM_CCER_CC2NE
#define LL_TIM_CHANNEL_CH3                     TIM_CCER_CC3E
#define LL_TIM_CHANNEL_CH3N                    TIM_CCER_CC3NE
#define LL_TIM_CHANNEL_CH4                     TIM_CCER_CC4E
#define LL_TIM_COUNTERMODE_UP                  0x00000000U
#define LL_TIM_COUNTERMODE_DOWN                (1U << 4)
#define LL_TIM_COUNTERMODE_CENTER_UP           (1U << 5)
#define LL_TIM_COUNTERMODE_CENTER_DOWN         (2U << 5)
#define LL_TIM_COUNTERMODE_CENTER_UP_DOWN      (3U << 5)
#define LL_TIM_CLOCKDIVISION_DIV1              0x00000000U
#define LL_TIM_CLOCKDIVISION_DIV2              (1U << 8)
#define LL_TIM_CLOCKDIVISION_DIV4              (2U << 8)
#define LL_TIM_OCMODE_FROZEN                   0x00000000U
#define LL_TIM_OCMODE_ACTIVE                   (1U << 4)
#define LL_TIM_OCMODE_INACTIVE                 (2U << 4)
#define LL_TIM_OCMODE_TOGGLE                   (3U << 4)
#define LL_TIM_OCMODE_FORCED_INACTIVE          (4U << 4)
#define LL_TIM_OCMODE_FORCED_ACTIVE            (5U << 4)
#define LL_TIM_OCMODE_PWM1                     (6U << 4)
#define LL_TIM_OCMODE_PWM2                     (7U << 4)
#define LL_TIM_OCSTATE_DISABLE                 0x00000000U
#define LL_TIM_OCPOLARITY_HIGH                 0x00000000U
#define LL_TIM_OCIDLESTATE_LOW                 0x00000000U
#define LL_TIM_TRGO_RESET                      0x00000000U
#define LL_TIM_ACTIVEINPUT_DIRECTTI            (TIM_CCMR1_CC1S_0 << 16U)
#define LL_TIM_ACTIVEINPUT_INDIRECTTI          (TIM_CCMR1_CC1S_1 << 16U)
#define LL_TIM_ACTIVEINPUT_TRC                 (TIM_CCMR1_CC1S << 16U)
#define LL_TIM_ICPSC_DIV1                      0x00000000U
#define LL_TIM_ICPSC_DIV2                      (TIM_CCMR1_IC1PSC_0 << 16U)
#define LL_TIM_ICPSC_DIV4                      (TIM_CCMR1_IC1PSC_1 << 16U)
#define LL_TIM_ICPSC_DIV8                      (TIM_CCMR1_IC1PSC << 16U)
#define LL_TIM_IC_FILTER_FDIV1                 0x00000000U
#define LL_TIM_IC_FILTER_FDIV4_N8              ((TIM_CCMR1_IC1F_2 | TIM_CCMR1_IC1F_1 | TIM_CCMR1_IC1F_0) << 16U)
#define LL_TIM_IC_POLARITY_RISING              0x00000000U
#define LL_TIM_IC_POLARITY_FALLING             TIM_CCER_CC1P
#define LL_TIM_IC_POLARITY_BOTHEDGE            (TIM_CCER_CC1P | TIM_CCER_CC1NP)


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


struct TIM_TypeDef {
	uint32 CR1 = 0;
	uint32 CR2 = 0;
	uint32 SMCR = 0;
	uint32 DIER = 0;
	uint32 SR = 0;
	uint32 EGR = 0;
	uint32 CCMR1 = 0;
	uint32 CCMR2 = 0;
	uint32 CCER = 0;
	uint32 CNT = 0;
	uint32 PSC = 0;
	uint32 ARR = 0;
	uint32 RCR = 0;
	uint32 CCR1 = 0;
	uint32 CCR2 = 0;
	uint32 CCR3 = 0;
	uint32 CCR4 = 0;
	uint32 BDTR = 0;
	uint32 DCR = 0;
	uint32 DMAR = 0;
	uint32 OR = 0;
};


struct LL_TIM_InitTypeDef {
	uint32 Prescaler = 0;
	uint32 CounterMode = 0;
	uint32 Autoreload = 0;
	uint32 ClockDivision = 0;
	uint32 RepetitionCounter = 0;
};


struct LL_TIM_OC_InitTypeDef {
	uint32 OCMode = 0;
	uint32 OCState = 0;
	uint32 OCNState = 0;
	uint32 CompareValue = 0;
	uint32 OCPolarity = 0;
	uint32 OCNPolarity = 0;
	uint32 OCIdleState = 0;
	uint32 OCNIdleState = 0;
};


// The timers the adapter may be given. TIM1 and TIM8 are the advanced ones.
inline TIM_TypeDef simTim1;
inline TIM_TypeDef simTim2;
inline TIM_TypeDef simTim8;

#define TIM1 (&simTim1)
#define TIM2 (&simTim2)
#define TIM8 (&simTim8)

#define LL_TIM_ReadReg(TIMx, REG) ((TIMx)->REG)
#define MODIFY_REG(REG, CLEARMASK, SETMASK) ((REG) = (((REG) & ~(CLEARMASK)) | (SETMASK)))
#define IS_TIM_CLOCK_DIVISION_INSTANCE(TIMx) ((TIMx) != nullptr)


// The channel index: 0 for channel 1 up to 3 for channel 4.
inline uint32 SimTimChannelIndex(uint32 channel) {
	switch (channel) {
		case LL_TIM_CHANNEL_CH2: return 1;
		case LL_TIM_CHANNEL_CH3: return 2;
		case LL_TIM_CHANNEL_CH4: return 3;
		default: return 0;
	}
}


// Writes one channel's byte of CCMR1 or CCMR2. The field is given inside the byte, and the value is shifted to the channel.
inline void SimTimSetCcmr(TIM_TypeDef *tim, uint32 channel, uint32 fieldMask, uint32 value) {
	uint32 index = SimTimChannelIndex(channel);
	uint32 shift = (index & 1U) != 0 ? 8U : 0U;
	uint32 *reg = (index < 2) ? &tim->CCMR1 : &tim->CCMR2;
	*reg = (*reg & ~(fieldMask << shift)) | ((value << shift) & (fieldMask << shift));
}


// Clears the flags written as 0 and keeps the others, as the hardware does.
inline void SimTimClearFlags(TIM_TypeDef *tim, uint32 flags) {
	tim->SR &= ~flags;
}


inline ErrorStatus LL_TIM_Init(TIM_TypeDef *tim, LL_TIM_InitTypeDef *init) {
	tim->PSC = init->Prescaler;
	tim->ARR = init->Autoreload;
	tim->RCR = init->RepetitionCounter;
	tim->CR1 = (tim->CR1 & ~TIM_CR1_CKD) | init->ClockDivision;
	return SUCCESS;
}

inline void LL_TIM_SetPrescaler(TIM_TypeDef *tim, uint32 prescaler) { tim->PSC = prescaler; }
inline void LL_TIM_SetAutoReload(TIM_TypeDef *tim, uint32 period) { tim->ARR = period; }
inline void LL_TIM_SetClockDivision(TIM_TypeDef *tim, uint32 division) { tim->CR1 = (tim->CR1 & ~TIM_CR1_CKD) | division; }
inline void LL_TIM_EnableCounter(TIM_TypeDef *tim) { tim->CR1 |= TIM_CR1_CEN; }
inline void LL_TIM_DisableCounter(TIM_TypeDef *tim) { tim->CR1 &= ~TIM_CR1_CEN; }
inline void LL_TIM_DisableARRPreload(TIM_TypeDef *tim) { tim->CR1 &= ~TIM_CR1_ARPE; }
inline void LL_TIM_GenerateEvent_UPDATE(TIM_TypeDef *tim) { tim->EGR |= TIM_EGR_UG; }
inline void LL_TIM_EnableAllOutputs(TIM_TypeDef *tim) { tim->BDTR |= TIM_BDTR_MOE; }
inline void LL_TIM_DisableMasterSlaveMode(TIM_TypeDef *tim) { tim->SMCR &= ~TIM_SMCR_MSM; }

inline void LL_TIM_SetTriggerOutput(TIM_TypeDef *tim, uint32 trigger) {
	tim->CR2 = (tim->CR2 & ~0x70U) | trigger;
}

inline void LL_TIM_CC_EnableChannel(TIM_TypeDef *tim, uint32 channels) { tim->CCER |= channels; }
inline void LL_TIM_CC_DisableChannel(TIM_TypeDef *tim, uint32 channels) { tim->CCER &= ~channels; }

inline void LL_TIM_SetRemap(TIM_TypeDef *tim, uint32 remap) { tim->OR = remap; }


// Output compare.
inline void LL_TIM_OC_SetCompareCH1(TIM_TypeDef *tim, uint32 compare) { tim->CCR1 = compare; }
inline void LL_TIM_OC_SetCompareCH2(TIM_TypeDef *tim, uint32 compare) { tim->CCR2 = compare; }
inline void LL_TIM_OC_SetCompareCH3(TIM_TypeDef *tim, uint32 compare) { tim->CCR3 = compare; }
inline void LL_TIM_OC_SetCompareCH4(TIM_TypeDef *tim, uint32 compare) { tim->CCR4 = compare; }
inline void LL_TIM_OC_SetMode(TIM_TypeDef *tim, uint32 channel, uint32 mode) { SimTimSetCcmr(tim, channel, 0x70U, mode); }
inline void LL_TIM_OC_EnablePreload(TIM_TypeDef *tim, uint32 channel) { SimTimSetCcmr(tim, channel, 0x08U, 0x08U); }
inline void LL_TIM_OC_DisableFast(TIM_TypeDef *tim, uint32 channel) { SimTimSetCcmr(tim, channel, 0x04U, 0); }

inline ErrorStatus LL_TIM_OC_Init(TIM_TypeDef *tim, uint32 channel, LL_TIM_OC_InitTypeDef *init) {
	SimTimSetCcmr(tim, channel, 0x70U, init->OCMode);
	return SUCCESS;
}


// Input capture. The active input, prescaler and filter arrive with their value 16 bits up, as the F4 header defines them.
inline void LL_TIM_IC_SetActiveInput(TIM_TypeDef *tim, uint32 channel, uint32 activeInput) {
	SimTimSetCcmr(tim, channel, TIM_CCMR1_CC1S, activeInput >> 16U);
}

inline void LL_TIM_IC_SetPrescaler(TIM_TypeDef *tim, uint32 channel, uint32 prescaler) {
	SimTimSetCcmr(tim, channel, TIM_CCMR1_IC1PSC, prescaler >> 16U);
}

inline void LL_TIM_IC_SetFilter(TIM_TypeDef *tim, uint32 channel, uint32 filter) {
	SimTimSetCcmr(tim, channel, TIM_CCMR1_IC1F, filter >> 16U);
}

// The LL polarity values carry CC1P at bit 1 and CC1NP at bit 3. Each channel's slot is 4 bits up from the one before, so the
// pair of channel 2 sits at 5 and 7, channel 3 at 9 and 11, and channel 4 at 13 and 15.
inline void LL_TIM_IC_SetPolarity(TIM_TypeDef *tim, uint32 channel, uint32 polarity) {
	static constexpr uint32 polaritySlot[4] = { 0, 4, 8, 12 };
	uint32 shift = polaritySlot[SimTimChannelIndex(channel)];
	tim->CCER = (tim->CCER & ~((TIM_CCER_CC1NP | TIM_CCER_CC1P) << shift)) | (polarity << shift);
}

inline uint32 LL_TIM_IC_GetCaptureCH1(const TIM_TypeDef *tim) { return tim->CCR1; }
inline uint32 LL_TIM_IC_GetCaptureCH2(const TIM_TypeDef *tim) { return tim->CCR2; }
inline uint32 LL_TIM_IC_GetCaptureCH3(const TIM_TypeDef *tim) { return tim->CCR3; }
inline uint32 LL_TIM_IC_GetCaptureCH4(const TIM_TypeDef *tim) { return tim->CCR4; }


// Interrupt enables.
inline void LL_TIM_EnableIT_UPDATE(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_UIE; }
inline void LL_TIM_EnableIT_CC1(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_CC1IE; }
inline void LL_TIM_EnableIT_CC2(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_CC2IE; }
inline void LL_TIM_EnableIT_CC3(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_CC3IE; }
inline void LL_TIM_EnableIT_CC4(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_CC4IE; }
inline void LL_TIM_EnableIT_COM(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_COMIE; }
inline void LL_TIM_EnableIT_TRIG(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_TIE; }
inline void LL_TIM_EnableIT_BRK(TIM_TypeDef *tim) { tim->DIER |= TIM_DIER_BIE; }
inline void LL_TIM_DisableIT_UPDATE(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_UIE; }
inline void LL_TIM_DisableIT_CC1(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_CC1IE; }
inline void LL_TIM_DisableIT_CC2(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_CC2IE; }
inline void LL_TIM_DisableIT_CC3(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_CC3IE; }
inline void LL_TIM_DisableIT_CC4(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_CC4IE; }
inline void LL_TIM_DisableIT_COM(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_COMIE; }
inline void LL_TIM_DisableIT_TRIG(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_TIE; }
inline void LL_TIM_DisableIT_BRK(TIM_TypeDef *tim) { tim->DIER &= ~TIM_DIER_BIE; }

inline uint32 LL_TIM_IsEnabledIT_UPDATE(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_UIE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_CC1(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_CC1IE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_CC2(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_CC2IE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_CC3(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_CC3IE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_CC4(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_CC4IE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_COM(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_COMIE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_TRIG(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_TIE) != 0; }
inline uint32 LL_TIM_IsEnabledIT_BRK(const TIM_TypeDef *tim) { return (tim->DIER & TIM_DIER_BIE) != 0; }

// Flags.
inline uint32 LL_TIM_IsActiveFlag_UPDATE(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_UIF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_CC1(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_CC1IF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_CC2(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_CC2IF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_CC3(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_CC3IF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_CC4(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_CC4IF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_COM(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_COMIF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_TRIG(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_TIF) != 0; }
inline uint32 LL_TIM_IsActiveFlag_BRK(const TIM_TypeDef *tim) { return (tim->SR & TIM_SR_BIF) != 0; }

inline void LL_TIM_ClearFlag_UPDATE(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_UIF); }
inline void LL_TIM_ClearFlag_CC1(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_CC1IF); }
inline void LL_TIM_ClearFlag_CC2(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_CC2IF); }
inline void LL_TIM_ClearFlag_CC3(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_CC3IF); }
inline void LL_TIM_ClearFlag_CC4(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_CC4IF); }
inline void LL_TIM_ClearFlag_COM(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_COMIF); }
inline void LL_TIM_ClearFlag_TRIG(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_TIF); }
inline void LL_TIM_ClearFlag_BRK(TIM_TypeDef *tim) { SimTimClearFlags(tim, TIM_SR_BIF); }
