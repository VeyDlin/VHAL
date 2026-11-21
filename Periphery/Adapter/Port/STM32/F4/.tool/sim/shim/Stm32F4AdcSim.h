#pragma once
// Register model of the F4 ADC for the host tests. It has the LL functions and constants that ADCAdapterF4.h reaches, and
// nothing else. The adapter's code paths are not decided by the constant values here (they are all zero, except where the
// model reads a flag), so the constants are plain zeros. The model holds the enable bit and the status flags, which the
// test sets by hand. A conversion never finishes on its own.
#include <System/System.h>


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


struct ADC_TypeDef {
	uint32 CR1 = 0;
	uint32 CR2 = 0;
	uint32 SR = 0;
	uint32 DR = 0;
};


struct ADC_Common_TypeDef {
	uint32 CCR = 0;
};


#define __LL_ADC_COMMON_INSTANCE(adc)  (static_cast<ADC_Common_TypeDef *>(nullptr))


// Register bits the model reads or writes.
#define ADC_CR2_ADON     (1U << 0)
#define ADC_CR1_AWDIE    (1U << 6)
#define ADC_CR1_JEOSIE   (1U << 7)
#define ADC_CR1_OVRIE    (1U << 26)
#define ADC_CR1_EOCIE    (1U << 5)
#define ADC_SR_AWD       (1U << 0)
#define ADC_SR_JEOS      (1U << 6)
#define ADC_SR_EOC       (1U << 1)
#define ADC_SR_OVR       (1U << 5)


struct LL_ADC_InitTypeDef {
	uint32 Resolution = 0;
	uint32 DataAlignment = 0;
	uint32 SequencersScanMode = 0;
};


struct LL_ADC_CommonInitTypeDef {
	uint32 CommonClock = 0;
};


struct LL_ADC_REG_InitTypeDef {
	uint32 TriggerSource = 0;
	uint32 SequencerLength = 0;
	uint32 SequencerDiscont = 0;
	uint32 ContinuousMode = 0;
	uint32 DMATransfer = 0;
};


// LL constants. Their values are not used by the paths under test.
#define LL_ADC_CHANNEL_0                       0U
#define LL_ADC_CHANNEL_1                       0U
#define LL_ADC_CHANNEL_2                       0U
#define LL_ADC_CHANNEL_3                       0U
#define LL_ADC_CHANNEL_4                       0U
#define LL_ADC_CHANNEL_5                       0U
#define LL_ADC_CHANNEL_6                       0U
#define LL_ADC_CHANNEL_7                       0U
#define LL_ADC_CHANNEL_8                       0U
#define LL_ADC_CHANNEL_9                       0U
#define LL_ADC_CHANNEL_10                      0U
#define LL_ADC_CHANNEL_11                      0U
#define LL_ADC_CHANNEL_12                      0U
#define LL_ADC_CHANNEL_13                      0U
#define LL_ADC_CHANNEL_14                      0U
#define LL_ADC_CHANNEL_15                      0U
#define LL_ADC_CHANNEL_16                      0U
#define LL_ADC_CHANNEL_17                      0U
#define LL_ADC_CHANNEL_18                      0U
#define LL_ADC_CLOCK_SYNC_PCLK_DIV4            0U
#define LL_ADC_DATA_ALIGN_LEFT                 0U
#define LL_ADC_DATA_ALIGN_RIGHT                0U
#define LL_ADC_INJ_TRIG_INDEPENDENT            0U
#define LL_ADC_REG_CONV_CONTINUOUS             0U
#define LL_ADC_REG_CONV_SINGLE                 0U
#define LL_ADC_REG_DMA_TRANSFER_NONE           0U
#define LL_ADC_REG_FLAG_EOC_UNITARY_CONV       0U
#define LL_ADC_REG_RANK_1                      0U
#define LL_ADC_REG_RANK_2                      0U
#define LL_ADC_REG_RANK_3                      0U
#define LL_ADC_REG_RANK_4                      0U
#define LL_ADC_REG_RANK_5                      0U
#define LL_ADC_REG_RANK_6                      0U
#define LL_ADC_REG_RANK_7                      0U
#define LL_ADC_REG_RANK_8                      0U
#define LL_ADC_REG_RANK_9                      0U
#define LL_ADC_REG_RANK_10                     0U
#define LL_ADC_REG_RANK_11                     0U
#define LL_ADC_REG_RANK_12                     0U
#define LL_ADC_REG_RANK_13                     0U
#define LL_ADC_REG_RANK_14                     0U
#define LL_ADC_REG_RANK_15                     0U
#define LL_ADC_REG_RANK_16                     0U
#define LL_ADC_REG_SEQ_DISCONT_DISABLE         0U
#define LL_ADC_REG_SEQ_SCAN_DISABLE            0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_2RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_3RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_4RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_5RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_6RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_7RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_8RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_9RANKS      0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_10RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_11RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_12RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_13RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_14RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_15RANKS     0U
#define LL_ADC_REG_SEQ_SCAN_ENABLE_16RANKS     0U
#define LL_ADC_REG_TRIG_EXT_EXTI_LINE11        0U
#define LL_ADC_REG_TRIG_EXT_TIM1_CH1           0U
#define LL_ADC_REG_TRIG_EXT_TIM1_CH2           0U
#define LL_ADC_REG_TRIG_EXT_TIM1_CH3           0U
#define LL_ADC_REG_TRIG_EXT_TIM2_CH2           0U
#define LL_ADC_REG_TRIG_EXT_TIM2_CH3           0U
#define LL_ADC_REG_TRIG_EXT_TIM2_CH4           0U
#define LL_ADC_REG_TRIG_EXT_TIM2_TRGO          0U
#define LL_ADC_REG_TRIG_EXT_TIM3_CH1           0U
#define LL_ADC_REG_TRIG_EXT_TIM3_TRGO          0U
#define LL_ADC_REG_TRIG_EXT_TIM4_CH4           0U
#define LL_ADC_REG_TRIG_EXT_TIM5_CH1           0U
#define LL_ADC_REG_TRIG_EXT_TIM5_CH2           0U
#define LL_ADC_REG_TRIG_EXT_TIM5_CH3           0U
#define LL_ADC_REG_TRIG_EXT_TIM8_CH1           0U
#define LL_ADC_REG_TRIG_EXT_TIM8_TRGO          0U
#define LL_ADC_REG_TRIG_SOFTWARE               0U
#define LL_ADC_RESOLUTION_6B                   0U
#define LL_ADC_RESOLUTION_8B                   0U
#define LL_ADC_RESOLUTION_10B                  0U
#define LL_ADC_RESOLUTION_12B                  0U
#define LL_ADC_SAMPLINGTIME_3CYCLES            0U
#define LL_ADC_SAMPLINGTIME_15CYCLES           0U
#define LL_ADC_SAMPLINGTIME_28CYCLES           0U
#define LL_ADC_SAMPLINGTIME_56CYCLES           0U
#define LL_ADC_SAMPLINGTIME_84CYCLES           0U
#define LL_ADC_SAMPLINGTIME_112CYCLES          0U
#define LL_ADC_SAMPLINGTIME_144CYCLES          0U
#define LL_ADC_SAMPLINGTIME_480CYCLES          0U
#define LL_ADC_SEQ_SCAN_DISABLE                0U
#define LL_ADC_SEQ_SCAN_ENABLE                 0U


inline ErrorStatus LL_ADC_Init(ADC_TypeDef *, LL_ADC_InitTypeDef *) { return SUCCESS; }
inline ErrorStatus LL_ADC_CommonInit(ADC_Common_TypeDef *, LL_ADC_CommonInitTypeDef *) { return SUCCESS; }
inline ErrorStatus LL_ADC_REG_Init(ADC_TypeDef *, LL_ADC_REG_InitTypeDef *) { return SUCCESS; }

inline uint32 LL_ADC_IsEnabled(ADC_TypeDef *adc) { return (adc->CR2 & ADC_CR2_ADON) != 0; }
inline void LL_ADC_Enable(ADC_TypeDef *adc) { adc->CR2 |= ADC_CR2_ADON; }

inline void LL_ADC_REG_SetFlagEndOfConversion(ADC_TypeDef *, uint32) { }
inline void LL_ADC_REG_SetSequencerRanks(ADC_TypeDef *, uint32, uint32) { }
inline void LL_ADC_SetChannelSamplingTime(ADC_TypeDef *, uint32, uint32) { }
inline void LL_ADC_REG_StartConversionSWStart(ADC_TypeDef *) { }
inline uint32 LL_ADC_REG_IsTriggerSourceSWStart(ADC_TypeDef *) { return 1; }
inline uint32 LL_ADC_REG_GetFlagEndOfConversion(ADC_TypeDef *) { return 0; }
inline uint16 LL_ADC_REG_ReadConversionData12(ADC_TypeDef *) { return 0; }

inline uint32 LL_ADC_INJ_IsTriggerSourceSWStart(ADC_TypeDef *) { return 0; }
inline uint32 LL_ADC_INJ_GetSequencerLength(ADC_TypeDef *) { return 0; }
inline uint32 LL_ADC_INJ_GetTrigAuto(ADC_TypeDef *) { return 0; }

inline uint32 LL_ADC_IsActiveFlag_AWD1(ADC_TypeDef *adc) { return (adc->SR & ADC_SR_AWD) != 0; }
inline uint32 LL_ADC_IsActiveFlag_EOCS(ADC_TypeDef *adc) { return (adc->SR & ADC_SR_EOC) != 0; }
inline uint32 LL_ADC_IsActiveFlag_JEOS(ADC_TypeDef *adc) { return (adc->SR & ADC_SR_JEOS) != 0; }
inline uint32 LL_ADC_IsActiveFlag_OVR(ADC_TypeDef *adc) { return (adc->SR & ADC_SR_OVR) != 0; }

inline uint32 LL_ADC_IsEnabledIT_AWD1(ADC_TypeDef *adc) { return (adc->CR1 & ADC_CR1_AWDIE) != 0; }
inline uint32 LL_ADC_IsEnabledIT_EOCS(ADC_TypeDef *adc) { return (adc->CR1 & ADC_CR1_EOCIE) != 0; }
inline uint32 LL_ADC_IsEnabledIT_JEOS(ADC_TypeDef *adc) { return (adc->CR1 & ADC_CR1_JEOSIE) != 0; }
inline uint32 LL_ADC_IsEnabledIT_OVR(ADC_TypeDef *adc) { return (adc->CR1 & ADC_CR1_OVRIE) != 0; }

inline void LL_ADC_ClearFlag_AWD1(ADC_TypeDef *adc) { adc->SR &= ~ADC_SR_AWD; }
inline void LL_ADC_ClearFlag_EOCS(ADC_TypeDef *adc) { adc->SR &= ~ADC_SR_EOC; }
inline void LL_ADC_ClearFlag_JEOS(ADC_TypeDef *adc) { adc->SR &= ~ADC_SR_JEOS; }
inline void LL_ADC_ClearFlag_OVR(ADC_TypeDef *adc) { adc->SR &= ~ADC_SR_OVR; }

inline void LL_ADC_EnableIT_EOCS(ADC_TypeDef *adc) { adc->CR1 |= ADC_CR1_EOCIE; }
inline void LL_ADC_EnableIT_OVR(ADC_TypeDef *adc) { adc->CR1 |= ADC_CR1_OVRIE; }
inline void LL_ADC_DisableIT_EOCS(ADC_TypeDef *adc) { adc->CR1 &= ~ADC_CR1_EOCIE; }
inline void LL_ADC_DisableIT_JEOS(ADC_TypeDef *adc) { adc->CR1 &= ~ADC_CR1_JEOSIE; }
inline void LL_ADC_DisableIT_OVR(ADC_TypeDef *adc) { adc->CR1 &= ~ADC_CR1_OVRIE; }
