#pragma once
#include "../../TIMAdapter.h"




using ATIM = class TIMAdapterG4;


class TIMAdapterG4: public TIMAdapter {
public:
	struct ClockDivision {
		static inline constexpr ClockDivisionOption D1 { LL_TIM_CLOCKDIVISION_DIV1 };
		static inline constexpr ClockDivisionOption D2 { LL_TIM_CLOCKDIVISION_DIV2 };
		static inline constexpr ClockDivisionOption D4 { LL_TIM_CLOCKDIVISION_DIV4 };
	};

	struct Counter {
		static inline constexpr CounterOption Up           { LL_TIM_COUNTERMODE_UP };
		static inline constexpr CounterOption Down         { LL_TIM_COUNTERMODE_DOWN };
		static inline constexpr CounterOption CenterUp     { LL_TIM_COUNTERMODE_CENTER_UP };
		static inline constexpr CounterOption CenterDown   { LL_TIM_COUNTERMODE_CENTER_DOWN };
		static inline constexpr CounterOption CenterUpDown { LL_TIM_COUNTERMODE_CENTER_UP_DOWN };
	};

	struct OutputCompare {
		static inline constexpr OutputCompareOption Frozen              { LL_TIM_OCMODE_FROZEN };
		static inline constexpr OutputCompareOption Active              { LL_TIM_OCMODE_ACTIVE };
		static inline constexpr OutputCompareOption Inactive            { LL_TIM_OCMODE_INACTIVE };
		static inline constexpr OutputCompareOption Toggle              { LL_TIM_OCMODE_TOGGLE };
		static inline constexpr OutputCompareOption ForcedInactive      { LL_TIM_OCMODE_FORCED_INACTIVE };
		static inline constexpr OutputCompareOption ForcedActive        { LL_TIM_OCMODE_FORCED_ACTIVE };
		static inline constexpr OutputCompareOption Pwm                 { LL_TIM_OCMODE_PWM1 };
		static inline constexpr OutputCompareOption Pwm2                { LL_TIM_OCMODE_PWM2 };
		static inline constexpr OutputCompareOption RetrigOpm1          { LL_TIM_OCMODE_RETRIG_OPM1 };
		static inline constexpr OutputCompareOption RetrigOpm2          { LL_TIM_OCMODE_RETRIG_OPM2 };
		static inline constexpr OutputCompareOption CombinedPwm1        { LL_TIM_OCMODE_COMBINED_PWM1 };
		static inline constexpr OutputCompareOption CombinedPwm2        { LL_TIM_OCMODE_COMBINED_PWM2 };
		static inline constexpr OutputCompareOption AsymmetricPwm1      { LL_TIM_OCMODE_ASSYMETRIC_PWM1 };
		static inline constexpr OutputCompareOption AsymmetricPwm2      { LL_TIM_OCMODE_ASSYMETRIC_PWM2 };
		static inline constexpr OutputCompareOption PulseOnCompare      { LL_TIM_OCMODE_PULSE_ON_COMPARE };
		static inline constexpr OutputCompareOption DirectionOutput     { LL_TIM_OCMODE_DIRECTION_OUTPUT };
	};

	struct Channel {
		static inline constexpr ChannelOption C1 { LL_TIM_CHANNEL_CH1,  LL_TIM_CHANNEL_CH1N };
		static inline constexpr ChannelOption C2 { LL_TIM_CHANNEL_CH2,  LL_TIM_CHANNEL_CH2N };
		static inline constexpr ChannelOption C3 { LL_TIM_CHANNEL_CH3,  LL_TIM_CHANNEL_CH3N };
		static inline constexpr ChannelOption C4 { LL_TIM_CHANNEL_CH4,  LL_TIM_CHANNEL_CH4N };
		static inline constexpr ChannelOption C5 { LL_TIM_CHANNEL_CH5,  0 };
		static inline constexpr ChannelOption C6 { LL_TIM_CHANNEL_CH6,  0 };
	};

	struct Interrupt {
		static inline constexpr InterruptOption CaptureCompare1 { 1 };
		static inline constexpr InterruptOption CaptureCompare2 { 2 };
		static inline constexpr InterruptOption CaptureCompare3 { 3 };
		static inline constexpr InterruptOption CaptureCompare4 { 4 };
		static inline constexpr InterruptOption Update          { 5 };
		static inline constexpr InterruptOption Break           { 6 };
		static inline constexpr InterruptOption Trigger         { 7 };
		static inline constexpr InterruptOption Commutation     { 8 };
	};

	struct OutputTrigger {
		static inline constexpr OutputTriggerOption Reset        	{ LL_TIM_TRGO_RESET };
		static inline constexpr OutputTriggerOption Enable       	{ LL_TIM_TRGO_ENABLE };
		static inline constexpr OutputTriggerOption Update       	{ LL_TIM_TRGO_UPDATE };
		static inline constexpr OutputTriggerOption CaptureCompare1 { LL_TIM_TRGO_CC1IF };
		static inline constexpr OutputTriggerOption OC1REF       	{ LL_TIM_TRGO_OC1REF };
		static inline constexpr OutputTriggerOption OC2REF       	{ LL_TIM_TRGO_OC2REF };
		static inline constexpr OutputTriggerOption OC3REF       	{ LL_TIM_TRGO_OC3REF };
		static inline constexpr OutputTriggerOption OC4REF       	{ LL_TIM_TRGO_OC4REF };
		static inline constexpr OutputTriggerOption EncoderClk   	{ LL_TIM_TRGO_ENCODERCLK };
	};

	struct OutputTrigger2 {
		static inline constexpr OutputTriggerOption2 Reset              { LL_TIM_TRGO2_RESET };
		static inline constexpr OutputTriggerOption2 Enable             { LL_TIM_TRGO2_ENABLE };
		static inline constexpr OutputTriggerOption2 Update             { LL_TIM_TRGO2_UPDATE };
		static inline constexpr OutputTriggerOption2 CaptureCompare1    { LL_TIM_TRGO2_CC1F };
		static inline constexpr OutputTriggerOption2 OC1REF             { LL_TIM_TRGO2_OC1 };
		static inline constexpr OutputTriggerOption2 OC2REF             { LL_TIM_TRGO2_OC2 };
		static inline constexpr OutputTriggerOption2 OC3REF             { LL_TIM_TRGO2_OC3 };
		static inline constexpr OutputTriggerOption2 OC4REF             { LL_TIM_TRGO2_OC4 };
		static inline constexpr OutputTriggerOption2 OC5REF             { LL_TIM_TRGO2_OC5 };
		static inline constexpr OutputTriggerOption2 OC6REF             { LL_TIM_TRGO2_OC6 };
		static inline constexpr OutputTriggerOption2 RisingFalling4     { LL_TIM_TRGO2_OC4_RISINGFALLING };
		static inline constexpr OutputTriggerOption2 RisingFalling6     { LL_TIM_TRGO2_OC6_RISINGFALLING };
		static inline constexpr OutputTriggerOption2 Rising4Rising6     { LL_TIM_TRGO2_OC4_RISING_OC6_RISING };
		static inline constexpr OutputTriggerOption2 Rising4Falling6    { LL_TIM_TRGO2_OC4_RISING_OC6_FALLING };
		static inline constexpr OutputTriggerOption2 Rising5Rising6     { LL_TIM_TRGO2_OC5_RISING_OC6_RISING };
		static inline constexpr OutputTriggerOption2 Rising5Falling6    { LL_TIM_TRGO2_OC5_RISING_OC6_FALLING };
	};

	struct InputPrescaler {
	   static inline constexpr InputPrescalerOption Div1 { LL_TIM_ICPSC_DIV1 };
	   static inline constexpr InputPrescalerOption Div2 { LL_TIM_ICPSC_DIV2 };
	   static inline constexpr InputPrescalerOption Div4 { LL_TIM_ICPSC_DIV4 };
	   static inline constexpr InputPrescalerOption Div8 { LL_TIM_ICPSC_DIV8 };
	};

	struct InputRemapping {
	   struct Timer1Channel1 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM1_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM1_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM1_TI1_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM1_TI1_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM1_TI1_RMP_COMP4 };
	   };

	   struct Timer2Channel1 {
		   static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM2_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM2_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM2_TI1_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM2_TI1_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM2_TI1_RMP_COMP4 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM2_TI1_RMP_COMP5 };
		   #endif
	   };

	   struct Timer2Channel2 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM2_TI2_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM2_TI2_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM2_TI2_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM2_TI2_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM2_TI2_RMP_COMP4 };
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM2_TI2_RMP_COMP6 };
		   #endif
	   };

	   struct Timer2Channel3 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM2_TI3_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM2_TI3_RMP_COMP4 };
	   };

	   struct Timer2Channel4 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM2_TI4_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM2_TI4_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM2_TI4_RMP_COMP2 };
	   };

	   // TIM3
	   struct Timer3Channel1 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM3_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM3_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM3_TI1_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM3_TI1_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM3_TI1_RMP_COMP4 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM3_TI1_RMP_COMP5 };
		   #endif
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM3_TI1_RMP_COMP6 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM3_TI1_RMP_COMP7 };
		   #endif
	   };

	   struct Timer3Channel2 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM3_TI2_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM3_TI2_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM3_TI2_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM3_TI2_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM3_TI2_RMP_COMP4 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM3_TI2_RMP_COMP5 };
		   #endif
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM3_TI2_RMP_COMP6 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM3_TI2_RMP_COMP7 };
		   #endif
	   };

	   struct Timer3Channel3 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM3_TI3_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM3_TI3_RMP_COMP3 };
	   };

	   struct Timer4Channel1 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM4_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM4_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM4_TI1_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM4_TI1_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM4_TI1_RMP_COMP4 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM4_TI1_RMP_COMP5 };
		   #endif
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM4_TI1_RMP_COMP6 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM4_TI1_RMP_COMP7 };
		   #endif
	   };

	   struct Timer4Channel2 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM4_TI2_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM4_TI2_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM4_TI2_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM4_TI2_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM4_TI2_RMP_COMP4 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM4_TI2_RMP_COMP5 };
		   #endif
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM4_TI2_RMP_COMP6 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM4_TI2_RMP_COMP7 };
		   #endif
	   };

	   struct Timer4Channel3 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM4_TI3_RMP_GPIO };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM4_TI3_RMP_COMP5 };
		   #endif
	   };

	   struct Timer4Channel4 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM4_TI4_RMP_GPIO };
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM4_TI4_RMP_COMP6 };
		   #endif
	   };

	   #if defined(TIM5)
		   struct Timer5Channel1 {
			   static inline constexpr InputRemappingOption GPIO   { LL_TIM_TIM5_TI1_RMP_GPIO };
			   static inline constexpr InputRemappingOption LSI    { LL_TIM_TIM5_TI1_RMP_LSI };
			   static inline constexpr InputRemappingOption LSE    { LL_TIM_TIM5_TI1_RMP_LSE };
			   static inline constexpr InputRemappingOption RTCWakeup { LL_TIM_TIM5_TI1_RMP_RTC_WK };
			   static inline constexpr InputRemappingOption Comp1  { LL_TIM_TIM5_TI1_RMP_COMP1 };
			   static inline constexpr InputRemappingOption Comp2  { LL_TIM_TIM5_TI1_RMP_COMP2 };
			   static inline constexpr InputRemappingOption Comp3  { LL_TIM_TIM5_TI1_RMP_COMP3 };
			   static inline constexpr InputRemappingOption Comp4  { LL_TIM_TIM5_TI1_RMP_COMP4 };
			   #if defined(COMP5)
			   	   static inline constexpr InputRemappingOption Comp5  { LL_TIM_TIM5_TI1_RMP_COMP5 };
			   #endif
			   #if defined(COMP6)
			   	   static inline constexpr InputRemappingOption Comp6  { LL_TIM_TIM5_TI1_RMP_COMP6 };
			   #endif
			   #if defined(COMP7)
			   	   static inline constexpr InputRemappingOption Comp7  { LL_TIM_TIM5_TI1_RMP_COMP7 };
			   #endif
		   };

		   struct Timer5Channel2 {
			   static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM5_TI2_RMP_GPIO };
			   static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM5_TI2_RMP_COMP1 };
			   static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM5_TI2_RMP_COMP2 };
			   static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM5_TI2_RMP_COMP3 };
			   static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM5_TI2_RMP_COMP4 };
			   #if defined(COMP5)
			   	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM5_TI2_RMP_COMP5 };
			   #endif
			   #if defined(COMP6)
			   	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM5_TI2_RMP_COMP6 };
			   #endif
			   #if defined(COMP7)
			   	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM5_TI2_RMP_COMP7 };
			   #endif
		   };
	   #endif

	   struct Timer8Channel1 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM8_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM8_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM8_TI1_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM8_TI1_RMP_COMP3 };
	       static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM8_TI1_RMP_COMP4 };
	   };

	   struct Timer15Channel1 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM15_TI1_RMP_GPIO };
	       static inline constexpr InputRemappingOption LSE   { LL_TIM_TIM15_TI1_RMP_LSE };
	       static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM15_TI1_RMP_COMP1 };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM15_TI1_RMP_COMP2 };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5 { LL_TIM_TIM15_TI1_RMP_COMP5 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM15_TI1_RMP_COMP7 };
		   #endif
	   };

	   struct Timer15Channel2 {
	       static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM15_TI2_RMP_GPIO };
	       static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM15_TI2_RMP_COMP2 };
	       static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM15_TI2_RMP_COMP3 };
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6 { LL_TIM_TIM15_TI2_RMP_COMP6 };
		   #endif
		   #if defined(COMP7)
	       	   static inline constexpr InputRemappingOption Comp7 { LL_TIM_TIM15_TI2_RMP_COMP7 };
		   #endif
	   };

	   struct Timer16Channel1 {
	       static inline constexpr InputRemappingOption GPIO      { LL_TIM_TIM16_TI1_RMP_GPIO };
		   #if defined(COMP6)
	       	   static inline constexpr InputRemappingOption Comp6     { LL_TIM_TIM16_TI1_RMP_COMP6 };
		   #endif
	       static inline constexpr InputRemappingOption MCO       { LL_TIM_TIM16_TI1_RMP_MCO };
	       static inline constexpr InputRemappingOption HSEDiv32  { LL_TIM_TIM16_TI1_RMP_HSE_32 };
	       static inline constexpr InputRemappingOption RTCWakeup { LL_TIM_TIM16_TI1_RMP_RTC_WK };
	       static inline constexpr InputRemappingOption LSE       { LL_TIM_TIM16_TI1_RMP_LSE };
	       static inline constexpr InputRemappingOption LSI       { LL_TIM_TIM16_TI1_RMP_LSI };
	   };

	   struct Timer17Channel1 {
	       static inline constexpr InputRemappingOption GPIO      { LL_TIM_TIM17_TI1_RMP_GPIO };
		   #if defined(COMP5)
	       	   static inline constexpr InputRemappingOption Comp5     { LL_TIM_TIM17_TI1_RMP_COMP5 };
		   #endif
	       static inline constexpr InputRemappingOption MCO       { LL_TIM_TIM17_TI1_RMP_MCO };
	       static inline constexpr InputRemappingOption HSEDiv32  { LL_TIM_TIM17_TI1_RMP_HSE_32 };
	       static inline constexpr InputRemappingOption RTCWakeup { LL_TIM_TIM17_TI1_RMP_RTC_WK };
	       static inline constexpr InputRemappingOption LSE       { LL_TIM_TIM17_TI1_RMP_LSE };
	       static inline constexpr InputRemappingOption LSI       { LL_TIM_TIM17_TI1_RMP_LSI };
	   };

	   #if defined(TIM20)
		   struct Timer20Channel1 {
			   static inline constexpr InputRemappingOption GPIO  { LL_TIM_TIM20_TI1_RMP_GPIO };
			   static inline constexpr InputRemappingOption Comp1 { LL_TIM_TIM20_TI1_RMP_COMP1 };
			   static inline constexpr InputRemappingOption Comp2 { LL_TIM_TIM20_TI1_RMP_COMP2 };
			   static inline constexpr InputRemappingOption Comp3 { LL_TIM_TIM20_TI1_RMP_COMP3 };
			   static inline constexpr InputRemappingOption Comp4 { LL_TIM_TIM20_TI1_RMP_COMP4 };
		   };
	   #endif
	};

	struct InputFilter {
	   static inline constexpr InputFilterOption None              { LL_TIM_IC_FILTER_FDIV1 };
	   static inline constexpr InputFilterOption Div1Samples2      { LL_TIM_IC_FILTER_FDIV1_N2 };
	   static inline constexpr InputFilterOption Div1Samples4      { LL_TIM_IC_FILTER_FDIV1_N4 };
	   static inline constexpr InputFilterOption Div1Samples8      { LL_TIM_IC_FILTER_FDIV1_N8 };
	   static inline constexpr InputFilterOption Div2Samples6      { LL_TIM_IC_FILTER_FDIV2_N6 };
	   static inline constexpr InputFilterOption Div2Samples8      { LL_TIM_IC_FILTER_FDIV2_N8 };
	   static inline constexpr InputFilterOption Div4Samples6      { LL_TIM_IC_FILTER_FDIV4_N6 };
	   static inline constexpr InputFilterOption Div4Samples8      { LL_TIM_IC_FILTER_FDIV4_N8 };
	   static inline constexpr InputFilterOption Div8Samples6      { LL_TIM_IC_FILTER_FDIV8_N6 };
	   static inline constexpr InputFilterOption Div8Samples8      { LL_TIM_IC_FILTER_FDIV8_N8 };
	   static inline constexpr InputFilterOption Div16Samples5     { LL_TIM_IC_FILTER_FDIV16_N5 };
	   static inline constexpr InputFilterOption Div16Samples6     { LL_TIM_IC_FILTER_FDIV16_N6 };
	   static inline constexpr InputFilterOption Div16Samples8     { LL_TIM_IC_FILTER_FDIV16_N8 };
	   static inline constexpr InputFilterOption Div32Samples5     { LL_TIM_IC_FILTER_FDIV32_N5 };
	   static inline constexpr InputFilterOption Div32Samples6     { LL_TIM_IC_FILTER_FDIV32_N6 };
	   static inline constexpr InputFilterOption Div32Samples8     { LL_TIM_IC_FILTER_FDIV32_N8 };
	};


public:
	TIMAdapterG4() { }
	TIMAdapterG4(TIM_TypeDef *timer, uint32 busClockHz, Bitness bit = Bitness::B16): TIMAdapter(timer, busClockHz, bit) { }





	virtual inline void IrqHandler() override {

		// Capture compare 1 event
		if (LL_TIM_IsActiveFlag_CC1(timHandle) && LL_TIM_IsEnabledIT_CC1(timHandle)) {
			LL_TIM_ClearFlag_CC1(timHandle);
			if ((timHandle->CCMR1 & TIM_CCMR1_CC1S) != 0x00U) {
				CallInputCaptureEvent(1);
			} else {
				CallOutputCompareEvent(1);
			}
		}


		// Capture compare 2 event
		if (LL_TIM_IsActiveFlag_CC2(timHandle) && LL_TIM_IsEnabledIT_CC2(timHandle)) {
			LL_TIM_ClearFlag_CC2(timHandle);
			if ((timHandle->CCMR1 & TIM_CCMR1_CC2S) != 0x00U) {
				CallInputCaptureEvent(2);
			} else {
				CallOutputCompareEvent(2);
			}
		}


		// Capture compare 3 event
		if (LL_TIM_IsActiveFlag_CC3(timHandle) && LL_TIM_IsEnabledIT_CC3(timHandle)) {
			LL_TIM_ClearFlag_CC3(timHandle);
			if ((timHandle->CCMR2 & TIM_CCMR2_CC3S) != 0x00U) {
				CallInputCaptureEvent(3);
			} else {
				CallOutputCompareEvent(3);
			}
		}


		// Capture compare 4 event
		if (LL_TIM_IsActiveFlag_CC4(timHandle) && LL_TIM_IsEnabledIT_CC4(timHandle)) {
			LL_TIM_ClearFlag_CC4(timHandle);
			if ((timHandle->CCMR2 & TIM_CCMR2_CC4S) != 0x00U) {
				CallInputCaptureEvent(4);
			} else {
				CallOutputCompareEvent(4);
			}
		}


		// TIM Update event
		if (LL_TIM_IsActiveFlag_UPDATE(timHandle) && LL_TIM_IsEnabledIT_UPDATE(timHandle)) {
			LL_TIM_ClearFlag_UPDATE(timHandle);
			CallUpdateEvent();
		}


		// TIM Break input event
		if (LL_TIM_IsActiveFlag_BRK(timHandle) && LL_TIM_IsEnabledIT_BRK(timHandle)) {
			LL_TIM_ClearFlag_BRK(timHandle);
			CallBreakInputEvent();
		}


		// TIM Trigger detection event
		if (LL_TIM_IsActiveFlag_TRIG(timHandle) && LL_TIM_IsEnabledIT_TRIG(timHandle)) {
			LL_TIM_ClearFlag_TRIG(timHandle);
			CallTriggerDetectionEvent();
		}


		// TIM commutation event
		if (LL_TIM_IsActiveFlag_COM(timHandle) && LL_TIM_IsEnabledIT_COM(timHandle)) {
			LL_TIM_ClearFlag_COM(timHandle);
			CallCommutationEvent();
		}
	}





public:
	virtual inline void EnableCounter(bool enableTimerCounter) override {
		if(enableTimerCounter) {
			LL_TIM_EnableCounter(timHandle);
		} else {
			LL_TIM_DisableCounter(timHandle);
		}
	}





	virtual inline void SetChannelsState(const std::initializer_list<EnableChannelParameters>& list) override {
		uint32 enableChannels = 0;
		uint32 disableChannels = 0;

		for(auto &channel : list) {
			switch (channel.channelSelect) {
				case ChannelEnableSelect::EnablePositive:
					enableChannels |= channel.channel.Get<1>();
				break;

				case ChannelEnableSelect::EnableNegative:
					enableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::EnableAll:
					enableChannels |= channel.channel.Get<1>();
					enableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::DisablePositive:
					disableChannels |= channel.channel.Get<1>();
				break;

				case ChannelEnableSelect::DisableNegative:
					disableChannels |= channel.channel.Get<2>();
				break;

				case ChannelEnableSelect::DisableAll:
					disableChannels |= channel.channel.Get<1>();
					disableChannels |= channel.channel.Get<2>();
				break;
			}
		}


		if(enableChannels != 0) {
			LL_TIM_CC_EnableChannel(timHandle, enableChannels);
		}

		if(disableChannels != 0) {
			LL_TIM_CC_DisableChannel(timHandle, disableChannels);
		}
	}





	virtual inline void SetDivision(ClockDivisionOption division) override {
		if (IS_TIM_CLOCK_DIVISION_INSTANCE(timHandle)) {
			auto CR1_REG = LL_TIM_ReadReg(timHandle, CR1);
			MODIFY_REG(CR1_REG, TIM_CR1_CKD, division.Get());
			parameters.division = division;
		}
	}





	virtual inline void SetPrescaler(uint32 prescaler) override {
		SystemAssert(parameters.prescaler <= 0xFFFF);
		LL_TIM_SetPrescaler(timHandle, prescaler);
		parameters.prescaler = prescaler;
	}





	virtual inline void SetPeriod(uint32 period) override {
		LL_TIM_SetAutoReload(timHandle, period);
		parameters.period = period;
	}





	virtual inline void SetCompare(ChannelOption channel, uint32 compare) override {
		if(channel == Channel::C1) {
			LL_TIM_OC_SetCompareCH1(timHandle, compare);
		} else if(channel == Channel::C2) {
			LL_TIM_OC_SetCompareCH2(timHandle, compare);
		} else if(channel == Channel::C3) {
			LL_TIM_OC_SetCompareCH3(timHandle, compare);
		} else if(channel == Channel::C4) {
			LL_TIM_OC_SetCompareCH4(timHandle, compare);
		}
		outputCompareParameters[GetChannelIndex(channel)].compare = compare;
	}





	virtual inline void SetCompareMode(ChannelOption channel, OutputCompareOption mode) override {
		LL_TIM_OC_SetMode(timHandle, channel.Get<1>(), mode.Get());
	}





	virtual inline uint32 GetCapture(ChannelOption channel) override {
		if(channel == Channel::C1) {
			return LL_TIM_IC_GetCaptureCH1(timHandle);
		} else if(channel == Channel::C2) {
			return LL_TIM_IC_GetCaptureCH2(timHandle);
		} else if(channel == Channel::C3) {
			return LL_TIM_IC_GetCaptureCH3(timHandle);
		} else if(channel == Channel::C4) {
			return LL_TIM_IC_GetCaptureCH4(timHandle);
		}
		SystemAbort();
		return 0;
	}





	virtual inline void GenerateUpdateEvent() override {
		LL_TIM_GenerateEvent_UPDATE(timHandle);
	}





	virtual uint16 GetClockDivision() override {
		if(parameters.division == ClockDivision::D1) {
			return 1;
		} else if(parameters.division == ClockDivision::D2) {
			return 2;
		} else if(parameters.division == ClockDivision::D4) {
			return 4;
		}

		return 1;
	}





	virtual uint8 GetChannelIndex(ChannelOption channel) override {
		if(channel == Channel::C1) {
			return 0;
		}
		if(channel == Channel::C2) {
			return 1;
		}
		if(channel == Channel::C3) {
			return 2;
		}
		if(channel == Channel::C4) {
			return 3;
		}

		SystemAbort();
		return 0;
	}



protected:
	virtual Status::statusType Initialization() override {
		SystemAssert(parameters.prescaler <= 0xFFFF);
		SystemAssert(parameters.repetitionCounter <= 0xFF);

		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_TIM_InitTypeDef init = {
			.Prescaler = static_cast<uint16>(parameters.prescaler),
			.CounterMode = parameters.mode.Get(),
			.Autoreload = parameters.period,
			.ClockDivision = parameters.division.Get(),
			.RepetitionCounter = static_cast<uint8>(parameters.repetitionCounter)
		};

		if(LL_TIM_Init(timHandle, &init) != ErrorStatus::SUCCESS) {
			return Status::error;
		}

		LL_TIM_DisableARRPreload(timHandle);

		return AfterInitialization();
	}





	virtual Status::statusType OutputCompareInitialization(const std::initializer_list<OutputCompareParameters>& list) override {
		for(auto &channel : list) {
			// TODO: [VHAL] [TIM] [G4] [WTF] only 1 ??
			LL_TIM_OC_EnablePreload(timHandle, channel.channel.Get<1>());

			LL_TIM_OC_InitTypeDef init = {
				.OCMode = channel.mode.Get(),

				.OCState = channel.positive.state == State::Disable ? LL_TIM_OCSTATE_DISABLE : LL_TIM_OCSTATE_ENABLE,
				.OCNState = channel.negative.state == State::Disable ? LL_TIM_OCSTATE_DISABLE : LL_TIM_OCSTATE_ENABLE,

				.CompareValue = channel.compare,

				.OCPolarity = channel.positive.polarity == Polarity::Low ? LL_TIM_OCPOLARITY_LOW : LL_TIM_OCPOLARITY_HIGH,
				.OCNPolarity = channel.negative.polarity == Polarity::Low ? LL_TIM_OCPOLARITY_LOW : LL_TIM_OCPOLARITY_HIGH,

				.OCIdleState = channel.positive.idleState == IdleState::Low ? LL_TIM_OCIDLESTATE_LOW : LL_TIM_OCIDLESTATE_HIGH,
				.OCNIdleState = channel.negative.idleState == IdleState::Low ? LL_TIM_OCIDLESTATE_LOW : LL_TIM_OCIDLESTATE_HIGH,
			};

			if(LL_TIM_OC_Init(timHandle, channel.channel.Get<1>(), &init) != ErrorStatus::SUCCESS) {
				return Status::error;
			}

			// TODO: [VHAL] [TIM] [G4] [WTF] channel 2 ??
			LL_TIM_OC_DisableFast(timHandle, channel.channel.Get<1>());
		}

		// TODO: [VHAL] [TIM] [ADAPTER] [ADD SUPPORT] Add Trigger to settings
		LL_TIM_SetTriggerOutput(timHandle, LL_TIM_TRGO_RESET);

		// TODO: [VHAL] [TIM] [ADAPTER] [ADD SUPPORT] Add Master Slave Mode to settings
		LL_TIM_DisableMasterSlaveMode(timHandle);

		if(timHandle == TIM1 || timHandle == TIM8) {
			LL_TIM_EnableAllOutputs(timHandle);
		}

		return Status::ok;
	}





	virtual Status::statusType InputCaptureInitialization(const std::initializer_list<InputCaptureParameters>& list) override {
		for(auto &channel : list) {
			LL_TIM_IC_SetActiveInput(timHandle, channel.channel.Get<1>(), CastInputSelection(channel));
			LL_TIM_IC_SetPrescaler(timHandle, channel.channel.Get<1>(), channel.prescaler.Get());
			LL_TIM_IC_SetFilter(timHandle, channel.channel.Get<1>(), channel.filter.Get());
			LL_TIM_IC_SetPolarity(timHandle, channel.channel.Get<1>(), CastInputPolarity(channel));
			LL_TIM_SetRemap(timHandle, channel.remapping.Get());
		}

		return Status::ok;
	}





	virtual Status::statusType BreakAndDeadTimeInitialization(const std::initializer_list<BreakAndDeadTimeParameters>& list) override {
		// TODO: [VHAL] [TIM] [G4] [ADD SUPPORT]
		return Status::notSupported;
	}





	virtual Status::statusType SetInterrupt(InterruptOption interrupt, bool enable) {
		if(interrupt == Interrupt::CaptureCompare1) {
			if (enable) {
				LL_TIM_EnableIT_CC1(timHandle);
			} else {
				LL_TIM_DisableIT_CC1(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare2) {
			if (enable) {
				LL_TIM_EnableIT_CC2(timHandle);
			} else {
				LL_TIM_DisableIT_CC2(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare3) {
			if (enable) {
				LL_TIM_EnableIT_CC3(timHandle);
			} else {
				LL_TIM_DisableIT_CC3(timHandle);
			}
		} else if(interrupt == Interrupt::CaptureCompare4) {
			if (enable) {
				LL_TIM_EnableIT_CC4(timHandle);
			} else {
				LL_TIM_DisableIT_CC4(timHandle);
			}
		} else if(interrupt == Interrupt::Update) {
			if (enable) {
				LL_TIM_EnableIT_UPDATE(timHandle);
			} else {
				LL_TIM_DisableIT_UPDATE(timHandle);
			}
		} else if(interrupt == Interrupt::Break) {
			if (enable) {
				LL_TIM_EnableIT_BRK(timHandle);
			} else {
				LL_TIM_DisableIT_BRK(timHandle);
			}
		} else if(interrupt == Interrupt::Trigger) {
			if (enable) {
				LL_TIM_EnableIT_TRIG(timHandle);
			} else {
				LL_TIM_DisableIT_TRIG(timHandle);
			}
		} else if(interrupt == Interrupt::Commutation) {
			if (enable) {
				LL_TIM_EnableIT_COM(timHandle);
			} else {
				LL_TIM_DisableIT_COM(timHandle);
			}
		}
		return Status::ok;
	}


private:
	constexpr uint32 CastInputSelection(const InputCaptureParameters& channel) const {
	    switch (channel.selection) {
			case InputSelection::Direct:   return LL_TIM_ACTIVEINPUT_DIRECTTI;
			case InputSelection::Indirect: return LL_TIM_ACTIVEINPUT_INDIRECTTI;
			case InputSelection::TRC:      return LL_TIM_ACTIVEINPUT_TRC;
	    }
	    System::Abort();
	    return 0;
	}


	constexpr uint32 CastInputPolarity(const InputCaptureParameters& channel) const {
	   switch (channel.polarity) {
		   case InputPolarity::Rising:        return LL_TIM_IC_POLARITY_RISING;
		   case InputPolarity::Falling:       return LL_TIM_IC_POLARITY_FALLING;
		   case InputPolarity::RisingFalling: return LL_TIM_IC_POLARITY_BOTHEDGE;
	   }
	   System::Abort();
	   return 0;
	}
};
