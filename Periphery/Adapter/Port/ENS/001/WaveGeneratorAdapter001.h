#pragma once
#include <Adapter/Port/ENS/Adapter/WaveGeneratorAdapter.h>


using AWaveGen = class WaveGeneratorAdapter001;

class WaveGeneratorAdapter001 : public WaveGeneratorAdapter<CMSDK_WAVE_GEN_TypeDef> {
public:
	struct Config {
		static inline constexpr ConfigOption None           { 0 };
		static inline constexpr ConfigOption RestEnable     { 1 << 0 };
		static inline constexpr ConfigOption NegativeEnable { 1 << 1 };
		static inline constexpr ConfigOption SilentEnable   { 1 << 2 };
		static inline constexpr ConfigOption SourceBEnable  { 1 << 3 };
		static inline constexpr ConfigOption Alternating    { 1 << 4 };
		static inline constexpr ConfigOption ContinueRepeat { 1 << 5 };
		static inline constexpr ConfigOption MultiElectrode { 1 << 6 };
	};

	struct CurrentSelect {
		static inline constexpr CurrentSelectOption I0 { 0 };
		static inline constexpr CurrentSelectOption I1 { 1 };
		static inline constexpr CurrentSelectOption I2 { 2 };
		static inline constexpr CurrentSelectOption I3 { 3 };
		static inline constexpr CurrentSelectOption I4 { 4 };
		static inline constexpr CurrentSelectOption I5 { 5 };
		static inline constexpr CurrentSelectOption I6 { 6 };
		static inline constexpr CurrentSelectOption I7 { 7 };
	};

	WaveGeneratorAdapter001() = default;
	WaveGeneratorAdapter001(CMSDK_WAVE_GEN_TypeDef *waveGen) : WaveGeneratorAdapter(waveGen) {}

	void SetParameters(Parameters val) override {
		parameters = val;
		Initialization();
	}

	void Start() override {
		waveGenHandle->WAVE_GEN_DRV_CTRL_REG |= 0x01;
	}

	void Stop() override {
		waveGenHandle->WAVE_GEN_DRV_CTRL_REG &= ~0x01;
	}

	void WriteWaveformPoint(uint8 address, uint8 value) override {
		waveGenHandle->WAVE_GEN_DRV_IN_WAVE_ADDR_REG = address;
		waveGenHandle->WAVE_GEN_DRV_IN_WAVE_REG = value;
	}

	void WriteWaveformPointDriverB(uint8 address, uint8 value, uint8 channel) override {
		waveGenHandle->WAVE_GEN_DRV_IN_WAVE_ADDR_REG = address;
		waveGenHandle->WAVE_GEN_DRV_IN_WAVE_REG = value | (static_cast<uint16>(channel) << 8);
	}

	void IrqHandler() override {
		// Read status before clearing (INT_REG has asymmetric read/write layout)
		uint32 intRead = waveGenHandle->WAVE_GEN_DRV_INT_REG;

		InterruptEvent event {
			.firstAddress = (intRead & CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_STS_Msk) != 0,
			.secondAddress = (intRead & CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_STS_Msk) != 0,
			.readFirstAddr = static_cast<uint8>((intRead & CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Msk) >> CMSDK_WAVE_GEN_DRV_INT_READ_FIRST_ADDR_Pos),
			.readSecondAddr = static_cast<uint8>((intRead & CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Msk) >> CMSDK_WAVE_GEN_DRV_INT_READ_SECOND_ADDR_Pos),
		};

		// Clear both interrupt flags
		waveGenHandle->WAVE_GEN_DRV_INT_REG = intRegShadow
			| CMSDK_WAVE_GEN_DRV_INT_FIRSTADDR_CLR_Msk
			| CMSDK_WAVE_GEN_DRV_INT_SECONDADDR_CLR_Msk;

		if (onInterrupt) {
			onInterrupt(event);
		}
	}

	void SetInterruptAddresses(uint8 firstAddress, uint8 secondAddress) override {
		intRegShadow &= ~(CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Msk | CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Msk);
		intRegShadow |= (static_cast<uint32>(firstAddress) << CMSDK_WAVE_GEN_DRV_INT_FIRST_ADDR_Pos);
		intRegShadow |= (static_cast<uint32>(secondAddress) << CMSDK_WAVE_GEN_DRV_INT_SECOND_ADDR_Pos);
		waveGenHandle->WAVE_GEN_DRV_INT_REG = intRegShadow;
	}

	void EnableInterrupt(bool enable) override {
		if (enable) {
			intRegShadow |= CMSDK_WAVE_GEN_DRV_INT_EN_Msk;
		} else {
			intRegShadow &= ~CMSDK_WAVE_GEN_DRV_INT_EN_Msk;
		}
		waveGenHandle->WAVE_GEN_DRV_INT_REG = intRegShadow;
	}


private:
	inline void OnEnableClock() {
		CMSDK_SYSCON->APB_CLKEN |= (1 << 12); // WAVE_GEN_PCLK_EN
	}


protected:
	Status::statusType Initialization() {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		waveGenHandle->WAVE_GEN_DRV_CONFIG_REG = parameters.config;
		waveGenHandle->WAVE_GEN_DRV_REST_T_REG = parameters.restTime;
		waveGenHandle->WAVE_GEN_DRV_SILENT_T_REG = parameters.silentTime;
		waveGenHandle->WAVE_GEN_DRV_HLF_WAVE_PRD_REG = parameters.halfWavePeriod;
		waveGenHandle->WAVE_GEN_DRV_NEG_HLF_WAVE_PRD_REG = parameters.negHalfWavePeriod;
		waveGenHandle->WAVE_GEN_DRV_CLK_FREQ_REG = parameters.clockFreqDiv;
		waveGenHandle->WAVE_GEN_DRV_ALT_LIM_REG = parameters.alternationLimit;
		waveGenHandle->WAVE_GEN_DRV_ALT_SILENT_LIM_REG = parameters.alternationSilentLimit;
		waveGenHandle->WAVE_GEN_DRV_DELAY_LIM_REG = parameters.delayLimit;
		waveGenHandle->WAVE_GEN_DRV_NEG_SCALE_REG = parameters.negScale;
		waveGenHandle->WAVE_GEN_DRV_NEG_OFFSET_REG = parameters.negOffset;
		waveGenHandle->WAVE_GEN_DRV_ISEL_REG = parameters.currentSelect;
		waveGenHandle->WAVE_GEN_DRV_SW_CONFIG_REG = parameters.switchConfig;

		return AfterInitialization();
	}
};
