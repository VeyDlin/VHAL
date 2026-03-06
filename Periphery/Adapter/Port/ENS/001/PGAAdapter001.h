#pragma once
#include <Adapter/Port/ENS/Adapter/PGAAdapter.h>


using APGA = class PGAAdapter001;

class PGAAdapter001 : public PGAAdapter<CMSDK_ANAC_TypeDef> {
public:
	struct NegativeInput {
		static inline constexpr NegativeInputOption VIN0        { 0 };
		static inline constexpr NegativeInputOption VIN1        { 1 };
		static inline constexpr NegativeInputOption InternalVCM { 2 };
		static inline constexpr NegativeInputOption ExternalVCM { 3 };
	};

	struct PositiveInput {
		static inline constexpr PositiveInputOption VIP0        { 0 };
		static inline constexpr PositiveInputOption VIP1        { 1 };
		static inline constexpr PositiveInputOption InternalVCM { 2 };
		static inline constexpr PositiveInputOption VBAT        { 3 };
		static inline constexpr PositiveInputOption AVDD1P8     { 4 };
		static inline constexpr PositiveInputOption VLCD        { 5 };
		static inline constexpr PositiveInputOption VRef1_2V    { 6 };
		static inline constexpr PositiveInputOption VTemp       { 7 };
	};

	// Gain: differential mode 1x-8x, single-ended 2x-9x
	struct Gain {
		static inline constexpr GainOption X1_X2 { 0 };
		static inline constexpr GainOption X2_X3 { 1 };
		static inline constexpr GainOption X3_X4 { 2 };
		static inline constexpr GainOption X4_X5 { 3 };
		static inline constexpr GainOption X5_X6 { 4 };
		static inline constexpr GainOption X6_X7 { 5 };
		static inline constexpr GainOption X7_X8 { 6 };
		static inline constexpr GainOption X8_X9 { 7 };
	};

	struct Output {
		static inline constexpr OutputOption ToGPIO4 { 0 };
		static inline constexpr OutputOption ToADC   { 1 };
	};


	PGAAdapter001() = default;
	PGAAdapter001(CMSDK_ANAC_TypeDef *anac) : PGAAdapter(anac) {}


	void SetParameters(Parameters val) override {
		parameters = val;
		Initialization();
	}

	void Enable() override {
		anacHandle->PGA_CTRL |= 0x03;
	}

	void Disable() override {
		anacHandle->PGA_CTRL &= ~0x03u;
	}


protected:
	Status::statusType Initialization() {
		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		uint32 ctrl = 0;
		ctrl |= (static_cast<uint32>(parameters.gain.Get()) & 0x07) << 4;
		ctrl |= (static_cast<uint32>(parameters.positiveInput.Get()) & 0x07) << 8;
		ctrl |= (static_cast<uint32>(parameters.negativeInput.Get()) & 0x03) << 12;
		anacHandle->PGA_CTRL = ctrl;

		return AfterInitialization();
	}
};
