#pragma once
#include <Adapter/ADCAdapter.h>


using AADC = class ADCAdapter001;


class ADCAdapter001 : public ADCAdapter<CMSDK_ADC_TypeDef> {
public:
	struct ClockDivider {
		static inline constexpr uint8 Div2  = 0;
		static inline constexpr uint8 Div4  = 1;
		static inline constexpr uint8 Div6  = 2;
		static inline constexpr uint8 Div8  = 3;
		static inline constexpr uint8 Div10 = 4;
		static inline constexpr uint8 Div12 = 5;
		static inline constexpr uint8 Div16 = 6;
		static inline constexpr uint8 Div32 = 7;
	};

protected:
	uint8 selectedChannel = 0;
	uint8 samplingTimeReg = 0;	// 0=2clk, 1=3clk, 2=4clk, 3=5clk
	uint8 clockDivider = 0;		// See ClockDivider struct for values


public:
	ADCAdapter001() { }
	ADCAdapter001(CMSDK_ADC_TypeDef *adc, uint8 clkDiv = 0):ADCAdapter(adc), clockDivider(clkDiv) { }



	virtual inline void IrqHandler() override {
		uint32 isr = adcHandle->ADC_ISR;

		// Overrun
		if (isr & (1 << 1)) {
			adcHandle->ADC_INT_CLR = (1 << 1);
			CallError(Error::Overrun);
			return;
		}

		// End of conversion
		if (isr & (1 << 0)) {
			adcHandle->ADC_INT_CLR = (1 << 0);

			lastData = adcHandle->ADC_DATA & 0x0FFF;

			switch (GetResolutionByte()) {
				case 1: *(uint8*)dataPointer = static_cast<uint8>(lastData);   break;
				case 2: *(uint16*)dataPointer = static_cast<uint16>(lastData); break;
				default: break;
			}
			dataPointer += GetResolutionByte();

			if (++dataCounter < dataNeed) {
				// Start next conversion
				adcHandle->ADC_CTRL |= (1 << 8); // ADC_START
				return;
			}

			// All conversions complete
			if (regularParameters.continuousMode == ContinuousMode::Single) {
				adcHandle->ADC_IER &= ~(1 << 0); // Disable EOC interrupt
				adcHandle->ADC_IER &= ~(1 << 1); // Disable OVERRUN interrupt
				dataNeed = 0;
			}

			dataCounter = 0;
			dataPointer = dataPointerOriginal;
			state = Status::ready;

			CallInterrupt(Irq::Conversion);
		}
	}



	virtual void AbortRegular() override {
		adcHandle->ADC_IER &= ~(1 << 0); // Disable EOC interrupt
		adcHandle->ADC_IER &= ~(1 << 1); // Disable OVERRUN interrupt
		adcHandle->ADC_CTRL &= ~(1 << 8); // Clear ADC_START
		state = Status::ready;
	}


	virtual void AbortInjected() override {
		state = Status::ready;
	}


	virtual void AbortWatchDog() override {
		state = Status::ready;
	}


	virtual void AbortSampling() override {
		state = Status::ready;
	}


	virtual void AbortConfigurationReady() override {
		state = Status::ready;
	}



	virtual Status::statusType Calibration() override {
		// ENS001 ADC has no calibration
		return Status::ok;
	}



private:
	inline void OnEnableClock() {
		CMSDK_SYSCON->APB_CLKEN |= (1 << 13); // ADC_PCLK_EN
	}


protected:
	virtual Status::statusType Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		// Disable ADC before configuration
		adcHandle->ADC_CTRL &= ~(1 << 0); // ADC_EN = 0

		// Set clock divider
		adcHandle->ADC_CLK_DIV = clockDivider & 0x07;

		// Set sampling time
		adcHandle->ADC_SAMP_TIME = samplingTimeReg & 0x03;

		// Configure mode: single conversion, no overrun, no wait
		adcHandle->ADC_CONFG = 0;

		if (regularParameters.continuousMode == ContinuousMode::Continuous) {
			adcHandle->ADC_CONFG |= (1 << 0); // COV_MODE = continuous
		}

		// Disable all interrupts initially
		adcHandle->ADC_IER = 0;

		// Clear any pending interrupt flags
		adcHandle->ADC_INT_CLR = (1 << 0) | (1 << 1);

		return AfterInitialization();
	}



	virtual Status::statusType RegularInitialization(uint8 rankLength) override {
		// ENS001 ADC supports only single channel at a time
		if (rankLength > 1) {
			return Status::notSupported;
		}

		return Status::ok;
	}



	virtual Status::statusType InjectedInitialization(uint8 rankLength) override {
		return Status::notSupported;
	}



	virtual Status::statusType ReadByteArray(uint8 *buffer, uint16 size) override {
		if (state != Status::ready) {
			return Status::busy;
		}

		state = Status::busy;
		dataNeed = size / GetResolutionByte();
		dataCounter = 0;
		dataPointer = buffer;
		uint32 tickStart = System::GetTick();

		// Enable ADC
		adcHandle->ADC_CTRL |= (1 << 0); // ADC_EN

		// Set channel
		adcHandle->ADC_CH_SEL = selectedChannel & 0x07;

		// Clear interrupt flags
		adcHandle->ADC_INT_CLR = (1 << 0) | (1 << 1);

		for (dataCounter = 0; dataCounter < dataNeed; dataCounter++) {
			// Start conversion
			adcHandle->ADC_CTRL |= (1 << 8); // ADC_START

			// Poll for EOC
			while (!(adcHandle->ADC_SR & (1 << 0))) { // EOC_FLAG
				if ((System::GetTick() - tickStart) > timeout) {
					state = Status::ready;
					return Status::timeout;
				}
			}

			// Read data
			lastData = adcHandle->ADC_DATA & 0x0FFF;

			switch (GetResolutionByte()) {
				case 1: *(uint8*)dataPointer = static_cast<uint8>(lastData);   break;
				case 2: *(uint16*)dataPointer = static_cast<uint16>(lastData); break;
				default: break;
			}
			dataPointer += GetResolutionByte();

			// Clear EOC
			adcHandle->ADC_INT_CLR = (1 << 0);
		}

		state = Status::ready;
		return Status::ok;
	}



	virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint16 size) override {
		if (state != Status::ready) {
			return Status::busy;
		}

		state = Status::busy;
		dataNeed = size / GetResolutionByte();
		dataCounter = 0;
		dataPointer = buffer;
		dataPointerOriginal = buffer;

		// Enable ADC
		adcHandle->ADC_CTRL |= (1 << 0); // ADC_EN

		// Set channel
		adcHandle->ADC_CH_SEL = selectedChannel & 0x07;

		// Clear interrupt flags
		adcHandle->ADC_INT_CLR = (1 << 0) | (1 << 1);

		// Enable EOC and OVERRUN interrupts
		adcHandle->ADC_IER |= (1 << 0); // EOC_INT_EN
		adcHandle->ADC_IER |= (1 << 1); // OVERRUN_INT_EN

		// Start conversion
		adcHandle->ADC_CTRL |= (1 << 8); // ADC_START

		return Status::ok;
	}



	virtual Status::info<float> SetRegularChannel(const RegularChannel &channel, uint8 rank) override {
		// ENS001 ADC only supports channels 0-7
		if (channel.channel > 7) {
			return { Status::notSupported };
		}

		selectedChannel = channel.channel;

		// Map maxSamplingCycles to hardware register: 0=2clk, 1=3clk, 2=4clk, 3=5clk
		if (channel.maxSamplingCycles >= 5) {
			samplingTimeReg = 3;
		} else if (channel.maxSamplingCycles >= 4) {
			samplingTimeReg = 2;
		} else if (channel.maxSamplingCycles >= 3) {
			samplingTimeReg = 1;
		} else {
			samplingTimeReg = 0;
		}
		adcHandle->ADC_SAMP_TIME = samplingTimeReg & 0x03;

		return { Status::ok, 0.0f };
	}



	virtual Status::info<float> SetInjectedChannel(const InjecteChannel &channel, uint8 rank) override {
		return { Status::notSupported };
	}

};
