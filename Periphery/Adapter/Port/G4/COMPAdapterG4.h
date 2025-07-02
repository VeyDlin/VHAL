 #pragma once
#include "../../COMPAdapter.h"

#include <System/System.h>


using ACOMP = class COMPAdapterG4;


class COMPAdapterG4: public COMPAdapter {
public:
	struct InputPlus {
		static inline constexpr InputPlusOption IO1        		{ LL_COMP_INPUT_PLUS_IO1 };
		static inline constexpr InputPlusOption IO2        		{ LL_COMP_INPUT_PLUS_IO2 };
	};

	struct InputMinus {
	    static inline constexpr InputMinusOption VRefInt1_4     { LL_COMP_INPUT_MINUS_1_4VREFINT };
	    static inline constexpr InputMinusOption VRefInt1_2     { LL_COMP_INPUT_MINUS_1_2VREFINT };
	    static inline constexpr InputMinusOption VRefInt3_4     { LL_COMP_INPUT_MINUS_3_4VREFINT };
	    static inline constexpr InputMinusOption VRefInt        { LL_COMP_INPUT_MINUS_VREFINT };
	    static inline constexpr InputMinusOption DAC1Channel1   { LL_COMP_INPUT_MINUS_DAC1_CH1 };
	    static inline constexpr InputMinusOption DAC1Channel2   { LL_COMP_INPUT_MINUS_DAC1_CH2 };
	    static inline constexpr InputMinusOption DAC2Channel1   { LL_COMP_INPUT_MINUS_DAC2_CH1 };
	    static inline constexpr InputMinusOption DAC3Channel1   { LL_COMP_INPUT_MINUS_DAC3_CH1 };
	    static inline constexpr InputMinusOption DAC3Channel2   { LL_COMP_INPUT_MINUS_DAC3_CH2 };
	    static inline constexpr InputMinusOption DAC4Channel1   { LL_COMP_INPUT_MINUS_DAC4_CH1 };
	    static inline constexpr InputMinusOption DAC4Channel2   { LL_COMP_INPUT_MINUS_DAC4_CH2 };
	    static inline constexpr InputMinusOption IO1            { LL_COMP_INPUT_MINUS_IO1 };
	    static inline constexpr InputMinusOption IO2            { LL_COMP_INPUT_MINUS_IO2 };
	};

	struct InputHysteresis {
	   static inline constexpr InputHysteresisOption None      	{ LL_COMP_HYSTERESIS_NONE };
	   static inline constexpr InputHysteresisOption mV10      	{ LL_COMP_HYSTERESIS_10MV };
	   static inline constexpr InputHysteresisOption mV20      	{ LL_COMP_HYSTERESIS_20MV };
	   static inline constexpr InputHysteresisOption mV30      	{ LL_COMP_HYSTERESIS_30MV };
	   static inline constexpr InputHysteresisOption mV40      	{ LL_COMP_HYSTERESIS_40MV };
	   static inline constexpr InputHysteresisOption mV50      	{ LL_COMP_HYSTERESIS_50MV };
	   static inline constexpr InputHysteresisOption mV60      	{ LL_COMP_HYSTERESIS_60MV };
	   static inline constexpr InputHysteresisOption mV70      	{ LL_COMP_HYSTERESIS_70MV };
	   static inline constexpr InputHysteresisOption Low       	{ LL_COMP_HYSTERESIS_LOW };
	   static inline constexpr InputHysteresisOption Medium    	{ LL_COMP_HYSTERESIS_MEDIUM };
	   static inline constexpr InputHysteresisOption High      	{ LL_COMP_HYSTERESIS_HIGH };
	};

	struct OutputBlankingSource {
	    static inline constexpr OutputBlankingSourceOption None         { LL_COMP_BLANKINGSRC_NONE };

	    // COMP1 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp1  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP1 };
	    static inline constexpr OutputBlankingSourceOption Timer2OutputCompare3Comp1  { LL_COMP_BLANKINGSRC_TIM2_OC3_COMP1 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3Comp1  { LL_COMP_BLANKINGSRC_TIM3_OC3_COMP1 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp1  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP1 };

	    // COMP2 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp2  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP2 };
	    static inline constexpr OutputBlankingSourceOption Timer2OutputCompare3Comp2  { LL_COMP_BLANKINGSRC_TIM2_OC3_COMP2 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3Comp2  { LL_COMP_BLANKINGSRC_TIM3_OC3_COMP2 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp2  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP2 };

	    // COMP3 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp3  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP3 };
	    static inline constexpr OutputBlankingSourceOption Timer2OutputCompare4Comp3  { LL_COMP_BLANKINGSRC_TIM2_OC4_COMP3 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3Comp3  { LL_COMP_BLANKINGSRC_TIM3_OC3_COMP3 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp3  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP3 };

	    // COMP4 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp4  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP4 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare4Comp4  { LL_COMP_BLANKINGSRC_TIM3_OC4_COMP4 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp4  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP4 };
	    static inline constexpr OutputBlankingSourceOption Timer15OutputCompare1Comp4 { LL_COMP_BLANKINGSRC_TIM15_OC1_COMP4 };

	    // COMP5 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp5  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP5 };
	    static inline constexpr OutputBlankingSourceOption Timer2OutputCompare3Comp5  { LL_COMP_BLANKINGSRC_TIM2_OC3_COMP5 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3Comp5  { LL_COMP_BLANKINGSRC_TIM3_OC3_COMP5 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp5  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP5 };

	    // COMP6 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp6  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP6 };
	    static inline constexpr OutputBlankingSourceOption Timer2OutputCompare4Comp6  { LL_COMP_BLANKINGSRC_TIM2_OC4_COMP6 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp6  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP6 };
	    static inline constexpr OutputBlankingSourceOption Timer15OutputCompare2Comp6 { LL_COMP_BLANKINGSRC_TIM15_OC2_COMP6 };

	    // COMP7 specific sources
	    static inline constexpr OutputBlankingSourceOption Timer1OutputCompare5Comp7  { LL_COMP_BLANKINGSRC_TIM1_OC5_COMP7 };
	    static inline constexpr OutputBlankingSourceOption Timer3OutputCompare3Comp7  { LL_COMP_BLANKINGSRC_TIM3_OC3_COMP7 };
	    static inline constexpr OutputBlankingSourceOption Timer8OutputCompare5Comp7  { LL_COMP_BLANKINGSRC_TIM8_OC5_COMP7 };
	    static inline constexpr OutputBlankingSourceOption Timer15OutputCompare2Comp7 { LL_COMP_BLANKINGSRC_TIM15_OC2_COMP7 };

	    // Common to all COMP instances
	    static inline constexpr OutputBlankingSourceOption Timer20OutputCompare5     { LL_COMP_BLANKINGSRC_TIM20_OC5 };
	    static inline constexpr OutputBlankingSourceOption Timer15OutputCompare1     { LL_COMP_BLANKINGSRC_TIM15_OC1 };
	    static inline constexpr OutputBlankingSourceOption Timer4OutputCompare3      { LL_COMP_BLANKINGSRC_TIM4_OC3 };
	};



public:
	COMPAdapterG4() = default;
	COMPAdapterG4(COMP_TypeDef *comp):COMPAdapter(comp) { }




	virtual inline void IrqHandler() override {
	    uint32 line = GetExtiLine(compHandle);
	    uint8 flag = 0;

		#if defined(COMP7)
			if ((COMPx == COMP6) || (COMPx == COMP7)) {
				if (LL_EXTI_IsActiveFlag_32_63(line) != 0) {
					flag = 2;
				}
			} else {
				if (LL_EXTI_IsActiveFlag_0_31(line) != 0) {
					flag = 1;
				}
			}
		#else
			if (LL_EXTI_IsActiveFlag_0_31(line) != 0) {
				flag = 1;
			}
		#endif

	    if (flag != 0) {
			#if defined(COMP7)
				if (flag == 2) {
					LL_EXTI_ClearFlag_32_63(line);
				} else {
					LL_EXTI_ClearFlag_0_31(line);
				}
			#else
				LL_EXTI_ClearFlag_0_31(line);
			#endif
			if (onInterrupt) {
				onInterrupt(GetOutputLevel().data);
			}
	    }
	}



	virtual Status::info<bool> GetOutputLevel() override {
		return LL_COMP_ReadOutputLevel(compHandle) == LL_COMP_OUTPUT_LEVEL_HIGH;
	};



	virtual Status::statusType Enable() override {
		if (LL_COMP_IsEnabled(compHandle)) {
			return Status::ok;
		}

		LL_COMP_Enable(compHandle);

	    if (
	    	parameters.inputMinus == InputMinus::VRefInt 	||
			parameters.inputMinus == InputMinus::VRefInt1_2 ||
			parameters.inputMinus == InputMinus::VRefInt1_4 ||
			parameters.inputMinus == InputMinus::VRefInt3_4
		) {
	        System::DelayUs(LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US);
	    } else {
	        System::DelayUs(LL_COMP_DELAY_STARTUP_US);
	    }

		return Status::ok;
	}


	virtual Status::statusType Disable() override {
		if (LL_COMP_IsEnabled(compHandle)) {
			LL_COMP_Disable(compHandle);
		}
		return Status::ok;
	}



protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_COMP_InitTypeDef init = {
			.InputPlus = parameters.inputPlus.Get(),
			.InputMinus = parameters.inputMinus.Get(),
			.InputHysteresis = parameters.inputHysteresis.Get(),
			.OutputPolarity = CastOutputPolarity(),
			.OutputBlankingSource = parameters.outputBlankingSource.Get()
		};

		SystemAssert(LL_COMP_Init(compHandle, &init) == ErrorStatus::SUCCESS);
		System::DelayUs(LL_COMP_DELAY_VOLTAGE_SCALER_STAB_US);

		// Manage EXTI settings
		uint32 extiLine = GetExtiLine(compHandle);
		if (parameters.triggerMode != TriggerMode::None) {
		    const uint8 triggerFlags = static_cast<uint8>(parameters.triggerMode);

		    if (triggerFlags & TriggerModeFlags::Rising) {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_EnableRisingTrig_32_63(extiLine);
		            } else {
		                LL_EXTI_EnableRisingTrig_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_EnableRisingTrig_0_31(extiLine);
		        #endif
		    } else {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_DisableRisingTrig_32_63(extiLine);
		            } else {
		                LL_EXTI_DisableRisingTrig_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_DisableRisingTrig_0_31(extiLine);
		        #endif
		    }

		    if (triggerFlags & TriggerModeFlags::Falling) {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_EnableFallingTrig_32_63(extiLine);
		            } else {
		                LL_EXTI_EnableFallingTrig_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_EnableFallingTrig_0_31(extiLine);
		        #endif
		    } else {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_DisableFallingTrig_32_63(extiLine);
		            } else {
		                LL_EXTI_DisableFallingTrig_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_DisableFallingTrig_0_31(extiLine);
		        #endif
		    }

		    #if defined(COMP7)
		        if ((compHandle == COMP6) || (compHandle == COMP7)) {
		            LL_EXTI_ClearFlag_32_63(extiLine);
		        } else {
		            LL_EXTI_ClearFlag_0_31(extiLine);
		        }
		    #else
		        LL_EXTI_ClearFlag_0_31(extiLine);
		    #endif

		    if (triggerFlags & TriggerModeFlags::Event) {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_EnableEvent_32_63(extiLine);
		            } else {
		                LL_EXTI_EnableEvent_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_EnableEvent_0_31(extiLine);
		        #endif
		    } else {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_DisableEvent_32_63(extiLine);
		            } else {
		                LL_EXTI_DisableEvent_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_DisableEvent_0_31(extiLine);
		        #endif
		    }

		    if (triggerFlags & TriggerModeFlags::Interrupt) {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_EnableIT_32_63(extiLine);
		            } else {
		                LL_EXTI_EnableIT_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_EnableIT_0_31(extiLine);
		        #endif
		    } else {
		        #if defined(COMP7)
		            if ((compHandle == COMP6) || (compHandle == COMP7)) {
		                LL_EXTI_DisableIT_32_63(extiLine);
		            } else {
		                LL_EXTI_DisableIT_0_31(extiLine);
		            }
		        #else
		            LL_EXTI_DisableIT_0_31(extiLine);
		        #endif
		    }
		} else {
		    #if defined(COMP7)
		        if ((compHandle == COMP6) || (compHandle == COMP7)) {
		            LL_EXTI_DisableEvent_32_63(extiLine);
		        } else {
		            LL_EXTI_DisableEvent_0_31(extiLine);
		        }
		    #else
		        LL_EXTI_DisableEvent_0_31(extiLine);
		    #endif

		    #if defined(COMP7)
		        if ((compHandle == COMP6) || (compHandle == COMP7)) {
		            LL_EXTI_DisableIT_32_63(extiLine);
		        } else {
		            LL_EXTI_DisableIT_0_31(extiLine);
		        }
		    #else
		        LL_EXTI_DisableIT_0_31(extiLine);
		    #endif
		}

		return AfterInitialization();
	}





private:
	constexpr uint32 CastOutputPolarity() const {
		switch (parameters.outputPolarity) {
			case OutputPolarity::NotInverted: return LL_COMP_OUTPUTPOL_NONINVERTED;
			case OutputPolarity::Inverted: return LL_COMP_OUTPUTPOL_INVERTED;
		}
		SystemAbort();
		return 0;
	}


	static constexpr uint32 GetExtiLine(COMP_TypeDef* instance) {
	    #if defined(STM32G414xx) || defined(STM32G474xx) || defined(STM32G484xx) || \
	        defined(STM32G473xx) || defined(STM32G483xx)
	    return (instance == COMP1) ? LL_EXTI_LINE_21 :
	           (instance == COMP2) ? LL_EXTI_LINE_22 :
	           (instance == COMP3) ? LL_EXTI_LINE_29 :
	           (instance == COMP4) ? LL_EXTI_LINE_30 :
	           (instance == COMP5) ? LL_EXTI_LINE_31 :
	           (instance == COMP6) ? LL_EXTI_LINE_32 :
	           (instance == COMP7) ? LL_EXTI_LINE_33 : 0;

	    #elif defined(STM32GBK1CB) || defined(STM32G431xx) || defined(STM32G441xx) || \
	          defined(STM32G471xx) || defined(STM32G491xx) || defined(STM32G4A1xx)
	    return (instance == COMP1) ? LL_EXTI_LINE_21 :
	           (instance == COMP2) ? LL_EXTI_LINE_22 :
	           (instance == COMP3) ? LL_EXTI_LINE_29 :
	           (instance == COMP4) ? LL_EXTI_LINE_30 : 0;

	    #elif defined(STM32G411xB) || defined(STM32G411xC)
	    return (instance == COMP1) ? LL_EXTI_LINE_21 :
	           (instance == COMP2) ? LL_EXTI_LINE_22 :
	           (instance == COMP3) ? LL_EXTI_LINE_29 : 0;

	    #else
	    	return 0;
	    #endif
	}
};















