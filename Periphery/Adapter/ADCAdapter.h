#pragma once
#include "IAdapter.h"
#include "Utilities/IOption.h"
#include <initializer_list>

#define AUSED_ADC_ADAPTER



class ADCAdapter: public IAdapter {
public:
    struct TriggerSourceOption : IOption<uint32> {
    	using IOption::IOption;
    };

    struct CommonSamplingOption : IOption<uint32> {
    	using IOption::IOption;
    };

	enum class Irq { Conversion, Injected, Watchdog, Sampling, ConfigReady };

	enum class Error { None, Overrun, ModeFault, FrameFormat, AdcConfig };

	enum class Resolution : uint8 { B6 = 6, B8 = 8, B10 = 10, B12 = 12, B14 = 14, B16 = 16 };
	enum class DataAlignment { Left, Right };
	enum class ScanMode { Enable, Disable };
	enum class ContinuousMode { Single, Continuous };

	struct Parameters {
		ScanMode scanMode = ScanMode::Enable;
		Resolution resolution = Resolution::B12;
		DataAlignment dataAlignment = DataAlignment::Right;
	};


	struct RegularParameters {
		TriggerSourceOption triggerSource;
		ContinuousMode continuousMode = ContinuousMode::Single;
	};


	struct InjectedParameters {
		TriggerSourceOption triggerSource;
		ContinuousMode continuousMode = ContinuousMode::Single;
	};


	struct RegularChannel {
		uint8 channel;
		uint16 maxSamplingCycles;
	};


	struct RegularChannelCommonSampling {
		uint8 channel;
		CommonSamplingOption commonSampling;
	};


	struct InjecteChannel {
		uint8 channel;
		uint16 maxSamplingCycles;
	};



protected:
	ADC_TypeDef *adcHandle;
	Parameters parameters;
	RegularParameters regularParameters;
	InjectedParameters injectedParameters;

	uint32 timeout = 1000;

	Status::statusType state = Status::ready;
	uint16 dataNeed = 0;
	uint16 dataCounter = 0;
	uint8 *dataPointer = nullptr;
	uint8 *dataPointerOriginal = nullptr;
	volatile uint16 lastData = 0;



public:
	std::function<void(Irq irqType, uint8 channel)> onInterrupt;
	std::function<void(Error errorType)> onError;



public:
	ADCAdapter() { }
	ADCAdapter(ADC_TypeDef *adc):adcHandle(adc) { }





	template <typename DataType>
	Status::statusType ReadArray(DataType* buffer, uint32 size = 1) {
		return ReadByteArray(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType>
	inline Status::info<DataType> Read() {
		auto output = Status::info<DataType>();
		output.type = ReadByteArray(reinterpret_cast<uint8*>(&output.data), sizeof(DataType));
		return output;
	}





	// ---------------





	template <typename DataType>
	inline Status::statusType ReadArrayAsync(DataType* buffer, uint32 size = 1) {
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(buffer), sizeof(DataType) * size);
	}


	template <typename DataType>
	inline Status::statusType ReadAsync(DataType &data) {
		return ReadByteArrayAsync(reinterpret_cast<uint8*>(&data), sizeof(DataType));
	}





public:
	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}



	virtual Status::statusType ConfigRegularGroup(RegularParameters val, const std::initializer_list<RegularChannel>& regularGroup) {
		regularParameters = val;
		auto regular = RegularInitialization(regularGroup.size());
		if(regular != Status::ok) {
			return regular;
		}

		uint8 rank = 1;
		for(auto &channel : regularGroup) {
			auto status = SetRegularChannel(channel, rank++);
			if(status.IsError()) {
				return status.type;
			}
		}

	    return Status::ok;
	}





	virtual Status::statusType ConfigInjectedGroup(const std::initializer_list<InjecteChannel>& injectedGroup) {
		uint8 rank = 1;
		for(auto &channel : injectedGroup) {
			auto status = SetInjectedChannel(channel, rank++);
			if(status.IsError()) {
				return status.type;
			}
		}

	    return InjectedInitialization(rank - 1);
	}





	// TODO: virtual Status::statusType ConfigCommonSampling(CCommonSampling commonSampling, uint16 maxSamplingCycles) = 0;





	void SetTimeout(uint32 val) {
		timeout = val;
	}



	virtual Status::statusType Calibration() = 0;



	virtual inline void IrqHandler() = 0;





	virtual void AbortRegular() = 0;
	virtual void AbortInjected() = 0;
	virtual void AbortWatchDog() = 0;
	virtual void AbortSampling() = 0;
	virtual void AbortConfigurationReady() = 0;

	virtual void AbortAll() {
		AbortRegular();
		AbortInjected();
		AbortWatchDog();
		AbortSampling();
		AbortConfigurationReady();
	}



protected:
	virtual Status::statusType Initialization() = 0;
	virtual Status::statusType RegularInitialization(uint8 rankLength) = 0;
	virtual Status::statusType InjectedInitialization(uint8 rankLength) = 0;


	virtual Status::statusType ReadByteArray(uint8 *buffer, uint16 size) = 0;
	virtual Status::statusType ReadByteArrayAsync(uint8 *buffer, uint16 size) = 0;

	virtual Status::info<float> SetRegularChannel(const RegularChannel &channel, uint8 rank) = 0;
	virtual Status::info<float> SetInjectedChannel(const InjecteChannel &channel, uint8 rank) = 0;




	virtual inline void CallInterrupt(Irq irqType, uint8 channel = 0) {
		if(onInterrupt != nullptr) {
			onInterrupt(irqType, channel);
		}
	}


	virtual inline void CallError(Error error) {
		if(onError != nullptr) {
			onError(error);
		}
	}


	virtual inline uint8 GetResolutionByte() {
		return static_cast<uint8>(parameters.resolution) > 8 ? 2 : 1;
	}


};
