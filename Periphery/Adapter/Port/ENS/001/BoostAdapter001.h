#pragma once
#include <Adapter/Port/ENS/Adapter/BoostAdapter.h>


using ABoost = class BoostAdapter001;

class BoostAdapter001 : public BoostAdapter<CMSDK_ANAC_TypeDef> {
public:
	struct VoltageSelect {
		static inline constexpr VoltageSelectOption V11 { 0 };
		static inline constexpr VoltageSelectOption V15 { 1 };
		static inline constexpr VoltageSelectOption V26 { 2 };
		static inline constexpr VoltageSelectOption V45 { 3 };
		static inline constexpr VoltageSelectOption V55 { 4 };
	};

	BoostAdapter001() = default;
	BoostAdapter001(CMSDK_ANAC_TypeDef *anac) : BoostAdapter(anac) {}

	ResultStatus SetParameters(Parameters val) override {
		parameters = val;
		return Initialization();
	}

	void Enable() override {
		anacHandle->BOOST_CTRL |= (1 << 0); // BOOST_EN
	}

	void Disable() override {
		anacHandle->BOOST_CTRL &= ~(1 << 0);
	}

	void Standby(bool enable) override {
		if (enable) {
			anacHandle->BOOST_CTRL |= (1 << 1);
		} else {
			anacHandle->BOOST_CTRL &= ~(1 << 1);
		}
	}

	bool IsEnabled() override {
		return (anacHandle->BOOST_CTRL & (1 << 0)) != 0;
	}


private:
	inline void OnEnableClock() {
		CMSDK_SYSCON->APB_CLKEN |= (1 << 14); // ANALOG_PCLK_EN
	}

	// Full BOOST_CTRL values from original HAL (includes EN + STANDBY bits)
	static constexpr uint32 GetBoostCtrlValue(VoltageSelectOption voltage) {
		if (voltage == VoltageSelect::V11) return 0x71013;
		if (voltage == VoltageSelect::V15) return 0x71113;
		if (voltage == VoltageSelect::V26) return 0x71213;
		if (voltage == VoltageSelect::V45) return 0xC1413;
		if (voltage == VoltageSelect::V55) return 0xC1713;
		return 0x71013;
	}


protected:
	ResultStatus Initialization() {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != ResultStatus::ok) {
			return status;
		}

		uint32 ctrl = GetBoostCtrlValue(parameters.voltage);

		// Strip EN (bit 0) and STANDBY (bit 1) — let user control via Enable()/Standby()
		ctrl &= ~0x03u;

		if (parameters.externalMode) {
			ctrl |= (1 << 2);
		}

		anacHandle->BOOST_CTRL = ctrl;
		anacHandle->PMU_CTRL |= 0x10; // BG_BUFFER_EN, preserve other PMU settings

		return AfterInitialization();
	}
};
