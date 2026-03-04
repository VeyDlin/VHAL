#pragma once
#include <System/System.h>
#include <Adapter/COMPAdapter.h>


using ACOMP = class COMPAdapter001;

class COMPAdapter001 : public COMPAdapter<CMSDK_ANAC_TypeDef> {
public:
	// Comparator instance selection
	enum class Instance : uint8 { Comparator0 = 0, Comparator1 = 1 };

	// ENS001 VREF voltage options (3-bit, <6:4> in COMPx_CTRL)
	struct VRefVoltage {
		static inline constexpr InputMinusOption V0_3 { 0 };
		static inline constexpr InputMinusOption V0_6 { 1 };
		static inline constexpr InputMinusOption V0_9 { 2 };
		static inline constexpr InputMinusOption V1_2 { 3 };
		static inline constexpr InputMinusOption V1_5 { 4 };
		static inline constexpr InputMinusOption V1_8 { 5 };
		static inline constexpr InputMinusOption V2_1 { 6 };
		static inline constexpr InputMinusOption V2_4 { 7 };
	};

	// Negative input selection (<3:2> in COMPx_CTRL)
	struct NegativeInput {
		static inline constexpr InputMinusOption VIN0 { 0 };
		static inline constexpr InputMinusOption VIN1 { 1 };
		static inline constexpr InputMinusOption VREF { 2 };
	};

	// Positive input selection (<1> in COMPx_CTRL): 0=VIP0, 1=VIP1
	struct PositiveInput {
		static inline constexpr InputPlusOption VIP0 { 0 };
		static inline constexpr InputPlusOption VIP1 { 1 };
	};


	COMPAdapter001() = default;
	COMPAdapter001(CMSDK_ANAC_TypeDef *anac, Instance inst)
		: COMPAdapter(anac), instance(inst) {}


	void IrqHandler() override {
		if (onInterrupt) {
			onInterrupt(GetOutputLevel());
		}
	}

	bool GetOutputLevel() override {
		return (GetCtrlReg() >> 8) & 0x01;
	}

	Status::statusType Enable() override {
		GetCtrlReg() |= 0x01;
		return Status::ok;
	}

	Status::statusType Disable() override {
		GetCtrlReg() &= ~0x01u;
		return Status::ok;
	}

	Status::statusType SetInputPlus(InputPlusOption inputPlus) override {
		auto &ctrl = GetCtrlReg();
		ctrl &= ~(0x01u << 1);
		ctrl |= (inputPlus.Get() & 0x01) << 1;
		parameters.inputPlus = inputPlus;
		return Status::ok;
	}

	Status::statusType SetInputMinus(InputMinusOption inputMinus) override {
		auto &ctrl = GetCtrlReg();
		ctrl &= ~(0x03u << 2);
		ctrl |= (inputMinus.Get() & 0x03) << 2;
		parameters.inputMinus = inputMinus;
		return Status::ok;
	}

	Status::statusType SetInputHysteresis(InputHysteresisOption inputHysteresis) override {
		// ENS001 comparators do not support hysteresis
		return Status::notSupported;
	}

	Status::statusType SetOutputPolarity(OutputPolarity polarity) override {
		// ENS001 comparators do not support output polarity inversion
		parameters.outputPolarity = polarity;
		return (polarity == OutputPolarity::NotInverted) ? Status::ok : Status::notSupported;
	}

	Status::statusType SetOutputBlankingSource(OutputBlankingSourceOption outputBlankingSource) override {
		// ENS001 comparators do not support blanking
		return Status::notSupported;
	}

	Status::statusType SetTriggerMode(TriggerMode triggerMode) override {
		// ENS001 comparators do not have EXTI trigger support
		parameters.triggerMode = triggerMode;
		return (triggerMode == TriggerMode::None) ? Status::ok : Status::notSupported;
	}

	// ENS001-specific: set VREF voltage (<6:4> in COMPx_CTRL)
	void SetVRefVoltage(uint8 vrefSel) {
		vrefVoltage = vrefSel & 0x07;

		auto &ctrl = GetCtrlReg();
		ctrl &= ~(0x07u << 4);
		ctrl |= (static_cast<uint32>(vrefVoltage)) << 4;

		// Enable shared VREF when using internal reference
		compHandle->COMP0_CTRL |= (1u << 7); // COMP_REF_EN (shared by both comparators)
	}


protected:
	Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		auto &ctrl = GetCtrlReg();
		ctrl = 0; // Reset

		ctrl |= (parameters.inputPlus.Get() & 0x01) << 1;   // Positive input <1>
		ctrl |= (parameters.inputMinus.Get() & 0x03) << 2;   // Negative input <3:2>

		// If VREF is selected as negative input, set voltage and enable COMP_REF_EN
		if (parameters.inputMinus == NegativeInput::VREF) {
			ctrl |= (static_cast<uint32>(vrefVoltage) & 0x07) << 4; // VREF voltage <6:4>
			compHandle->COMP0_CTRL |= (1u << 7); // COMP_REF_EN (shared by both comparators)
		}

		return AfterInitialization();
	}


private:
	Instance instance = Instance::Comparator0;
	uint8 vrefVoltage = 0; // VREF voltage selection <6:4>, default 0 = 0.3V

	volatile uint32 &GetCtrlReg() {
		return (instance == Instance::Comparator0)
			? compHandle->COMP0_CTRL
			: compHandle->COMP1_CTRL;
	}

	volatile uint32 GetCtrlReg() const {
		return (instance == Instance::Comparator0)
			? compHandle->COMP0_CTRL
			: compHandle->COMP1_CTRL;
	}
};
