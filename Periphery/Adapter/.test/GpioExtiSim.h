#pragma once
// Host model of the EXTI block and of the GPIO calls that the STM32 G0, G4 and F4 GPIO ports reach. It keeps the interrupt
// mask, the event mask, the trigger selection, the pending bits and the port selection of each line. The rising and falling
// pending bits are separate registers in the model: the G0 port clears them apart, and the G4 and F4 ports read and clear
// both through the single-register calls, which here cover both bits. A test includes this header before its port header.
#include <VHAL.h>


struct GPIO_TypeDef {
	uint32 unused = 0;
};


inline GPIO_TypeDef simGpioA;
inline GPIO_TypeDef simGpioB;
#define GPIOA (&simGpioA)
#define GPIOB (&simGpioB)


enum class ErrorStatus { SUCCESS, ERROR };


struct LL_GPIO_InitTypeDef {
	uint32 Pin;
	uint32 Mode;
	uint32 Speed;
	uint32 OutputType;
	uint32 Pull;
	uint32 Alternate;
};


#define LL_GPIO_MODE_INPUT          0u
#define LL_GPIO_MODE_OUTPUT         1u
#define LL_GPIO_MODE_ALTERNATE      2u
#define LL_GPIO_MODE_ANALOG         3u
#define LL_GPIO_SPEED_FREQ_LOW      0u
#define LL_GPIO_SPEED_FREQ_MEDIUM   1u
#define LL_GPIO_SPEED_FREQ_HIGH     2u
#define LL_GPIO_SPEED_FREQ_VERY_HIGH 3u
#define LL_GPIO_OUTPUT_PUSHPULL     0u
#define LL_GPIO_OUTPUT_OPENDRAIN    1u
#define LL_GPIO_PULL_NO             0u
#define LL_GPIO_PULL_UP             1u
#define LL_GPIO_PULL_DOWN           2u


#define LL_IOP_GRP1_PERIPH_GPIOA 0u
#define LL_IOP_GRP1_PERIPH_GPIOB 1u
#define LL_IOP_GRP1_PERIPH_GPIOC 2u
#define LL_IOP_GRP1_PERIPH_GPIOD 3u
#define LL_IOP_GRP1_PERIPH_GPIOE 4u
#define LL_IOP_GRP1_PERIPH_GPIOF 5u
#define LL_IOP_GRP1_PERIPH_GPIOG 6u
#define LL_IOP_GRP1_PERIPH_GPIOH 7u
#define LL_IOP_GRP1_PERIPH_GPIOI 8u
#define LL_IOP_GRP1_PERIPH_GPIOJ 9u
#define LL_IOP_GRP1_PERIPH_GPIOK 10u
#define LL_AHB1_GRP1_PERIPH_GPIOA 0u
#define LL_AHB1_GRP1_PERIPH_GPIOB 1u
#define LL_AHB1_GRP1_PERIPH_GPIOC 2u
#define LL_AHB1_GRP1_PERIPH_GPIOD 3u
#define LL_AHB1_GRP1_PERIPH_GPIOE 4u
#define LL_AHB1_GRP1_PERIPH_GPIOF 5u
#define LL_AHB1_GRP1_PERIPH_GPIOG 6u
#define LL_AHB1_GRP1_PERIPH_GPIOH 7u
#define LL_AHB1_GRP1_PERIPH_GPIOI 8u
#define LL_AHB1_GRP1_PERIPH_GPIOJ 9u
#define LL_AHB1_GRP1_PERIPH_GPIOK 10u
#define LL_AHB2_GRP1_PERIPH_GPIOA 0u
#define LL_AHB2_GRP1_PERIPH_GPIOB 1u
#define LL_AHB2_GRP1_PERIPH_GPIOC 2u
#define LL_AHB2_GRP1_PERIPH_GPIOD 3u
#define LL_AHB2_GRP1_PERIPH_GPIOE 4u
#define LL_AHB2_GRP1_PERIPH_GPIOF 5u
#define LL_AHB2_GRP1_PERIPH_GPIOG 6u
#define LL_APB2_GRP1_PERIPH_SYSCFG 0u


inline void LL_IOP_GRP1_EnableClock(uint32) { }
inline void LL_AHB1_GRP1_EnableClock(uint32) { }
inline void LL_AHB2_GRP1_EnableClock(uint32) { }
inline void LL_APB2_GRP1_EnableClock(uint32) { }

inline ErrorStatus LL_GPIO_Init(GPIO_TypeDef *, LL_GPIO_InitTypeDef *) { return ErrorStatus::SUCCESS; }
inline void LL_GPIO_SetPinPull(GPIO_TypeDef *, uint32, uint32) { }
inline void LL_GPIO_SetPinMode(GPIO_TypeDef *, uint32, uint32) { }
inline uint32 LL_GPIO_IsOutputPinSet(GPIO_TypeDef *, uint32) { return 0; }
inline uint32 LL_GPIO_IsInputPinSet(GPIO_TypeDef *, uint32) { return 0; }
inline void LL_GPIO_SetOutputPin(GPIO_TypeDef *, uint32) { }
inline void LL_GPIO_ResetOutputPin(GPIO_TypeDef *, uint32) { }
inline void LL_GPIO_TogglePin(GPIO_TypeDef *, uint32) { }


// Line numbers and port selections, with the values of the G0 (LL_EXTI_CONFIG) and the G4 and F4 (LL_SYSCFG_EXTI) symbols.
#define LL_EXTI_CONFIG_LINE0 0u
#define LL_EXTI_CONFIG_LINE1 1u
#define LL_EXTI_CONFIG_LINE2 2u
#define LL_EXTI_CONFIG_LINE3 3u
#define LL_EXTI_CONFIG_LINE4 4u
#define LL_EXTI_CONFIG_LINE5 5u
#define LL_EXTI_CONFIG_LINE6 6u
#define LL_EXTI_CONFIG_LINE7 7u
#define LL_EXTI_CONFIG_LINE8 8u
#define LL_EXTI_CONFIG_LINE9 9u
#define LL_EXTI_CONFIG_LINE10 10u
#define LL_EXTI_CONFIG_LINE11 11u
#define LL_EXTI_CONFIG_LINE12 12u
#define LL_EXTI_CONFIG_LINE13 13u
#define LL_EXTI_CONFIG_LINE14 14u
#define LL_EXTI_CONFIG_LINE15 15u
#define LL_EXTI_CONFIG_PORTA 0u
#define LL_EXTI_CONFIG_PORTB 1u
#define LL_EXTI_CONFIG_PORTC 2u
#define LL_EXTI_CONFIG_PORTD 3u
#define LL_EXTI_CONFIG_PORTE 4u
#define LL_EXTI_CONFIG_PORTF 5u

#define LL_SYSCFG_EXTI_LINE0 0u
#define LL_SYSCFG_EXTI_LINE1 1u
#define LL_SYSCFG_EXTI_LINE2 2u
#define LL_SYSCFG_EXTI_LINE3 3u
#define LL_SYSCFG_EXTI_LINE4 4u
#define LL_SYSCFG_EXTI_LINE5 5u
#define LL_SYSCFG_EXTI_LINE6 6u
#define LL_SYSCFG_EXTI_LINE7 7u
#define LL_SYSCFG_EXTI_LINE8 8u
#define LL_SYSCFG_EXTI_LINE9 9u
#define LL_SYSCFG_EXTI_LINE10 10u
#define LL_SYSCFG_EXTI_LINE11 11u
#define LL_SYSCFG_EXTI_LINE12 12u
#define LL_SYSCFG_EXTI_LINE13 13u
#define LL_SYSCFG_EXTI_LINE14 14u
#define LL_SYSCFG_EXTI_LINE15 15u
#define LL_SYSCFG_EXTI_PORTA 0u
#define LL_SYSCFG_EXTI_PORTB 1u
#define LL_SYSCFG_EXTI_PORTC 2u
#define LL_SYSCFG_EXTI_PORTD 3u
#define LL_SYSCFG_EXTI_PORTE 4u
#define LL_SYSCFG_EXTI_PORTF 5u
#define LL_SYSCFG_EXTI_PORTG 6u
#define LL_SYSCFG_EXTI_PORTH 7u
#define LL_SYSCFG_EXTI_PORTI 8u
#define LL_SYSCFG_EXTI_PORTJ 9u
#define LL_SYSCFG_EXTI_PORTK 10u


struct SimExti {
	uint32 imr = 0;
	uint32 emr = 0;
	uint32 rtsr = 0;
	uint32 ftsr = 0;
	uint32 rpr = 0;
	uint32 fpr = 0;
	uint32 source[16] = {};
};


inline SimExti simExti;


inline uint32 LL_EXTI_IsEnabledIT_0_31(uint32 l) { return (simExti.imr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsEnabledEvent_0_31(uint32 l) { return (simExti.emr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsEnabledRisingTrig_0_31(uint32 l) { return (simExti.rtsr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsEnabledFallingTrig_0_31(uint32 l) { return (simExti.ftsr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsActiveRisingFlag_0_31(uint32 l) { return (simExti.rpr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsActiveFallingFlag_0_31(uint32 l) { return (simExti.fpr & l) == l ? 1u : 0u; }
inline uint32 LL_EXTI_IsActiveFlag_0_31(uint32 l) { return ((simExti.rpr | simExti.fpr) & l) != 0 ? 1u : 0u; }
inline void LL_EXTI_ClearRisingFlag_0_31(uint32 l) { simExti.rpr &= ~l; }
inline void LL_EXTI_ClearFallingFlag_0_31(uint32 l) { simExti.fpr &= ~l; }
inline void LL_EXTI_ClearFlag_0_31(uint32 l) { simExti.rpr &= ~l; simExti.fpr &= ~l; }
inline void LL_EXTI_EnableIT_0_31(uint32 l) { simExti.imr |= l; }
inline void LL_EXTI_DisableIT_0_31(uint32 l) { simExti.imr &= ~l; }
inline void LL_EXTI_EnableEvent_0_31(uint32 l) { simExti.emr |= l; }
inline void LL_EXTI_DisableEvent_0_31(uint32 l) { simExti.emr &= ~l; }
inline void LL_EXTI_EnableRisingTrig_0_31(uint32 l) { simExti.rtsr |= l; }
inline void LL_EXTI_DisableRisingTrig_0_31(uint32 l) { simExti.rtsr &= ~l; }
inline void LL_EXTI_EnableFallingTrig_0_31(uint32 l) { simExti.ftsr |= l; }
inline void LL_EXTI_DisableFallingTrig_0_31(uint32 l) { simExti.ftsr &= ~l; }

inline uint32 LL_EXTI_GetEXTISource(uint32 line) { return simExti.source[line]; }
inline void LL_EXTI_SetEXTISource(uint32 port, uint32 line) { simExti.source[line] = port; }
inline uint32 LL_SYSCFG_GetEXTISource(uint32 line) { return simExti.source[line]; }
inline void LL_SYSCFG_SetEXTISource(uint32 port, uint32 line) { simExti.source[line] = port; }
