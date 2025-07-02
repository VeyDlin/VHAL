#pragma once
#include "IAdapter.h"
#include "Utilities/IOption.h"

#define AUSED_DAC_ADAPTER



class COMPAdapter: public IAdapter {
public:
	struct TriggerModeFlags {
		constexpr static uint8 Interrupt 	= 0x01;
		constexpr static uint8 Event 		= 0x02;
		constexpr static uint8 Rising 		= 0x10;
		constexpr static uint8 Falling 		= 0x20;
	};

	enum class OutputPolarity { NotInverted, Inverted };

	enum class TriggerMode : uint8 {
	    None                   = 0,
	    InterruptRising        = TriggerModeFlags::Interrupt 	| TriggerModeFlags::Rising,
	    InterruptFalling       = TriggerModeFlags::Interrupt 	| TriggerModeFlags::Falling,
	    InterruptRisingFalling = TriggerModeFlags::Interrupt 	| TriggerModeFlags::Rising 		| TriggerModeFlags::Falling,
	    EventRising            = TriggerModeFlags::Event 		| TriggerModeFlags::Rising,
	    EventFalling           = TriggerModeFlags::Event 		| TriggerModeFlags::Falling,
	    EventRisingFalling     = TriggerModeFlags::Event 		| TriggerModeFlags::Rising 		| TriggerModeFlags::Falling
	};

    struct InputPlusOption : IOption<uint32> {
    	using IOption::IOption;
    };

    struct InputMinusOption : IOption<uint32> {
    	using IOption::IOption;
    };

    struct InputHysteresisOption : IOption<uint32> {
    	using IOption::IOption;
    };

    struct OutputBlankingSourceOption : IOption<uint32> {
    	using IOption::IOption;
    };


	struct Parameters {
    	InputPlusOption inputPlus;
    	InputMinusOption inputMinus;
    	InputHysteresisOption inputHysteresis;
    	OutputBlankingSourceOption outputBlankingSource;
		OutputPolarity outputPolarity = OutputPolarity::NotInverted;
    	TriggerMode triggerMode = TriggerMode::None;
	};


protected:
	COMP_TypeDef* compHandle;
	Parameters parameters;


public:
	std::function<void(bool outputLevel)> onInterrupt;


public:
	COMPAdapter() = default;
	COMPAdapter(COMP_TypeDef *comp):compHandle(comp) { }


	virtual inline void IrqHandler() = 0;


	virtual void SetParameters(Parameters val) {
		parameters = val;
		Initialization();
		Enable();
	}

	virtual Status::info<bool> GetOutputLevel() = 0;
	virtual Status::statusType Enable() = 0;
	virtual Status::statusType Disable() = 0;

protected:
	virtual Status::statusType Initialization() = 0;
};















