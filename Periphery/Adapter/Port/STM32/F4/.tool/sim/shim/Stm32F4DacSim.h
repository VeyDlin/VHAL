#pragma once
// Register model of the F4 DAC for the host tests. It has the LL functions and constants that DACAdapterF4.h reaches, and
// nothing else. The trigger values follow the F4 header in one respect that matters here: LL_DAC_TRIG_EXT_TIM6_TRGO is zero,
// and software is not. The model records what the adapter passes to LL_DAC_Init and how often the channel is enabled.
#include <System/System.h>


typedef enum {
	SUCCESS = 0,
	ERROR = 1
} ErrorStatus;


struct DAC_TypeDef {
	uint32 CR = 0;
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


inline ErrorStatus LL_DAC_Init(DAC_TypeDef *, uint32, LL_DAC_InitTypeDef *init) {
	simDacInitCount++;
	simDacLastTrigger = init->TriggerSource;
	return SUCCESS;
}

inline void LL_DAC_Enable(DAC_TypeDef *, uint32) { simDacEnableCount++; }
inline void LL_DAC_Disable(DAC_TypeDef *, uint32) { }
inline uint32 LL_DAC_IsEnabled(DAC_TypeDef *, uint32) { return 0; }

inline void LL_DAC_EnableDMAReq(DAC_TypeDef *, uint32) { }
inline void LL_DAC_DisableDMAReq(DAC_TypeDef *, uint32) { }
inline void LL_DAC_EnableTrigger(DAC_TypeDef *, uint32) { }
inline void LL_DAC_DisableTrigger(DAC_TypeDef *, uint32) { }

inline void LL_DAC_ConvertData12RightAligned(DAC_TypeDef *, uint32, uint32) { }
inline uint32 LL_DAC_DMA_GetRegAddr(DAC_TypeDef *, uint32, uint32) { return 0; }

inline uint32 LL_DAC_IsActiveFlag_DMAUDR1(DAC_TypeDef *) { return 0; }
inline uint32 LL_DAC_IsActiveFlag_DMAUDR2(DAC_TypeDef *) { return 0; }
inline void LL_DAC_ClearFlag_DMAUDR1(DAC_TypeDef *) { }
inline void LL_DAC_ClearFlag_DMAUDR2(DAC_TypeDef *) { }
