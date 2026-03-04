#pragma once
#include <System/System.h>
#include <Adapter/Port/ENS/Adapter/PMUAdapter.h>


using APMU = class PMUAdapter001;

class PMUAdapter001 : public PMUAdapter<CMSDK_ANAC_TypeDef> {
public:
	struct LvdThreshold {
		static inline constexpr LvdThresholdOption V4_2 { 0 };
		static inline constexpr LvdThresholdOption V3_9 { 1 };
		static inline constexpr LvdThresholdOption V3_6 { 2 };
		static inline constexpr LvdThresholdOption V3_3 { 3 };
		static inline constexpr LvdThresholdOption V3_0 { 4 };
		static inline constexpr LvdThresholdOption V2_7 { 5 };
		static inline constexpr LvdThresholdOption V2_4 { 6 };
		static inline constexpr LvdThresholdOption V2_1 { 7 };
	};


	PMUAdapter001() = default;
	PMUAdapter001(CMSDK_ANAC_TypeDef *anac) : PMUAdapter(anac) {}


	// LVD
	void SetLvdThreshold(LvdThresholdOption threshold) override {
		anacHandle->PMU_CTRL &= ~(0x07u << 1);
		anacHandle->PMU_CTRL |= (static_cast<uint32>(threshold.Get()) & 0x07) << 1;
	}

	void EnableLvd(bool enable) override {
		if (enable) {
			anacHandle->PMU_CTRL |= (1 << 0);
		} else {
			anacHandle->PMU_CTRL &= ~(1u << 0);
		}
	}

	bool GetLvdStatus() const override {
		return (anacHandle->PMU_CTRL >> 6) & 0x01;
	}

	// Temperature protection (150C trigger)
	void EnableTemperatureProtection(bool enable) override {
		if (enable) {
			anacHandle->PMU_CTRL |= (1 << 5);
		} else {
			anacHandle->PMU_CTRL &= ~(1u << 5);
		}
	}

	bool GetTemperatureAlert() const override {
		// Auto-enable if not already enabled
		return (anacHandle->PMU_CTRL >> 7) & 0x01;
	}

	// Bandgap buffer
	void EnableBandgap(bool enable) override {
		if (enable) {
			anacHandle->PMU_CTRL |= (1 << 4);
		} else {
			anacHandle->PMU_CTRL &= ~(1u << 4);
		}
	}
};
