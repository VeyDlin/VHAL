#pragma once
// Register model of the F4 DAC for the host tests. It has the LL functions and constants that DACAdapterF4.h reaches, and
// nothing else. The trigger values follow the F4 header in one respect that matters here: LL_DAC_TRIG_EXT_TIM6_TRGO is zero,
// and software is not. The model keeps CR and SR with the CMSIS bit positions, so a test reads the enable, trigger, DMA request
// and underrun bits the adapter wrote. The model records what the adapter passes to LL_DAC_Init and how often the channel is
// enabled.
#include <System/System.h>


// CR bits, channel 1 then channel 2.
#define DAC_CR_EN1           (1U << 0)
#define DAC_CR_TEN1          (1U << 1)
#define DAC_CR_DMAEN1        (1U << 12)
#define DAC_CR_DMAUDRIE1     (1U << 13)
#define DAC_CR_EN2           (1U << 16)
#define DAC_CR_TEN2          (1U << 17)
#define DAC_CR_DMAEN2        (1U << 28)
#define DAC_CR_DMAUDRIE2     (1U << 29)

// SR bits: the underrun flags of the two channels.
#define DAC_SR_DMAUDR1       (1U << 13)
#define DAC_SR_DMAUDR2       (1U << 29)


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


struct DAC_TypeDef {
	uint32 CR = 0;
	uint32 SR = 0;
};


struct LL_DAC_InitTypeDef {
	uint32 TriggerSource = 0;
	uint32 WaveAutoGeneration = 0;
	uint32 WaveAutoGenerationConfig = 0;
	uint32 OutputBuffer = 0;
};


// LL constants.
#define LL_DAC_CHANNEL_1                          1U
#define LL_DAC_CHANNEL_2                          2U
#define LL_DAC_DELAY_STARTUP_VOLTAGE_SETTLING_US  15UL
#define LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED  0x00000000U
#define LL_DAC_NOISE_LFSR_UNMASK_BIT0             0x00000000UL
#define LL_DAC_OUTPUT_BUFFER_ENABLE               0x00000000UL
#define LL_DAC_OUTPUT_BUFFER_DISABLE              0x00000002UL
#define LL_DAC_WAVE_AUTO_GENERATION_NONE          0x00000000U
#define LL_DAC_TRIG_EXT_TIM6_TRGO                 0x00000000UL
#define LL_DAC_TRIG_EXT_TIM7_TRGO                 0x00000008UL
#define LL_DAC_TRIG_EXT_TIM8_TRGO                 0x00000010UL
#define LL_DAC_TRIG_EXT_TIM5_TRGO                 0x00000018UL
#define LL_DAC_TRIG_EXT_TIM2_TRGO                 0x00000020UL
#define LL_DAC_TRIG_EXT_TIM4_TRGO                 0x00000028UL
#define LL_DAC_TRIG_EXT_EXTI_LINE9                0x00000030UL
#define LL_DAC_TRIG_SOFTWARE                      0x00000038UL


// Model state. The test resets it before each case.
inline uint32 simDacInitCount = 0;
inline uint32 simDacEnableCount = 0;
inline uint32 simDacLastTrigger = 0;


inline void SimDacResetModel() {
	simDacInitCount = 0;
	simDacEnableCount = 0;
	simDacLastTrigger = 0;
}


// The bit of a channel in each register: channel 1 uses the first group, channel 2 the second.
inline uint32 SimDacBit(uint32 channel, uint32 bitChannel1, uint32 bitChannel2) {
	return channel == LL_DAC_CHANNEL_2 ? bitChannel2 : bitChannel1;
}


inline ErrorStatus LL_DAC_Init(DAC_TypeDef *, uint32, LL_DAC_InitTypeDef *init) {
	simDacInitCount++;
	simDacLastTrigger = init->TriggerSource;
	return SUCCESS;
}


inline void LL_DAC_Enable(DAC_TypeDef *dac, uint32 channel) {
	simDacEnableCount++;
	dac->CR |= SimDacBit(channel, DAC_CR_EN1, DAC_CR_EN2);
}


inline void LL_DAC_Disable(DAC_TypeDef *dac, uint32 channel) {
	dac->CR &= ~SimDacBit(channel, DAC_CR_EN1, DAC_CR_EN2);
}


inline uint32 LL_DAC_IsEnabled(DAC_TypeDef *dac, uint32 channel) {
	return (dac->CR & SimDacBit(channel, DAC_CR_EN1, DAC_CR_EN2)) != 0;
}


inline void LL_DAC_EnableDMAReq(DAC_TypeDef *dac, uint32 channel) {
	dac->CR |= SimDacBit(channel, DAC_CR_DMAEN1, DAC_CR_DMAEN2);
}


inline void LL_DAC_DisableDMAReq(DAC_TypeDef *dac, uint32 channel) {
	dac->CR &= ~SimDacBit(channel, DAC_CR_DMAEN1, DAC_CR_DMAEN2);
}


inline void LL_DAC_EnableTrigger(DAC_TypeDef *dac, uint32 channel) {
	dac->CR |= SimDacBit(channel, DAC_CR_TEN1, DAC_CR_TEN2);
}


inline void LL_DAC_DisableTrigger(DAC_TypeDef *dac, uint32 channel) {
	dac->CR &= ~SimDacBit(channel, DAC_CR_TEN1, DAC_CR_TEN2);
}


inline void LL_DAC_ConvertData12RightAligned(DAC_TypeDef *, uint32, uint32) { }


// The 12-bit right-aligned holding register of each channel: DHR12R1 at offset 0x08 and DHR12R2 at 0x14 from DAC base 0x40007400.
inline uint32 LL_DAC_DMA_GetRegAddr(const DAC_TypeDef *, uint32 channel, uint32) {
	return 0x40007400U + (channel == LL_DAC_CHANNEL_2 ? 0x14U : 0x08U);
}


inline uint32 LL_DAC_IsActiveFlag_DMAUDR1(const DAC_TypeDef *dac) { return (dac->SR & DAC_SR_DMAUDR1) != 0; }
inline uint32 LL_DAC_IsActiveFlag_DMAUDR2(const DAC_TypeDef *dac) { return (dac->SR & DAC_SR_DMAUDR2) != 0; }
inline void LL_DAC_ClearFlag_DMAUDR1(DAC_TypeDef *dac) { dac->SR &= ~DAC_SR_DMAUDR1; }
inline void LL_DAC_ClearFlag_DMAUDR2(DAC_TypeDef *dac) { dac->SR &= ~DAC_SR_DMAUDR2; }

inline void LL_DAC_EnableIT_DMAUDR1(DAC_TypeDef *dac) { dac->CR |= DAC_CR_DMAUDRIE1; }
inline void LL_DAC_EnableIT_DMAUDR2(DAC_TypeDef *dac) { dac->CR |= DAC_CR_DMAUDRIE2; }
inline void LL_DAC_DisableIT_DMAUDR1(DAC_TypeDef *dac) { dac->CR &= ~DAC_CR_DMAUDRIE1; }
inline void LL_DAC_DisableIT_DMAUDR2(DAC_TypeDef *dac) { dac->CR &= ~DAC_CR_DMAUDRIE2; }
inline uint32 LL_DAC_IsEnabledIT_DMAUDR1(const DAC_TypeDef *dac) { return (dac->CR & DAC_CR_DMAUDRIE1) != 0; }
inline uint32 LL_DAC_IsEnabledIT_DMAUDR2(const DAC_TypeDef *dac) { return (dac->CR & DAC_CR_DMAUDRIE2) != 0; }
