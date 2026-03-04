#pragma once
#include "IAdapter.h"
#include "Utilities/IOption.h"
#include <functional>

#define AUSED_DAC_ADAPTER


template<typename HandleType, typename DMAHandleType = void*>
class DACAdapter: public IAdapter {
public:
	enum class OutputBuffer { Enable, Disable };
	enum class OutputConnection { External, Internal };
	enum class OutputMode { Normal, SampleHold };

	enum class Error { None, DmaUnderrun };

	struct TriggerSourceOption : IOption<uint32> {
		using IOption::IOption;
	};

	struct Parameters {
		OutputBuffer outputBuffer = OutputBuffer::Enable;
		OutputConnection outputConnection = OutputConnection::External;
		OutputMode outputMode = OutputMode::Normal;
		TriggerSourceOption triggerSource;
	};


protected:
	HandleType *dacHandle;
	Parameters parameters;
	uint8 dacChannel = 1;
	DMAHandleType *dma = nullptr;



public:
	std::function<void(Error errorType)> onError;


public:
	DACAdapter() = default;
	DACAdapter(HandleType *dac, uint8 channel):dacHandle(dac), dacChannel(channel) { }


	virtual inline void IrqHandler() = 0;


	virtual Status::statusType Write(uint16 val) = 0;


	virtual void SetParameters(Parameters val) {
		parameters = val;
		Initialization();
		Enable();
	}


	virtual Status::statusType Enable() = 0;
	virtual Status::statusType Disable() = 0;


	virtual void SetDMA(DMAHandleType *dmaAdapter) {
		dma = dmaAdapter;
	}


	virtual void EnableDMARequest() = 0;
	virtual void DisableDMARequest() = 0;
	virtual void EnableTrigger() = 0;
	virtual void DisableTrigger() = 0;


	virtual Status::statusType WriteContinuous(uint16 *buffer, uint32 count) = 0;
	virtual Status::statusType StopDMA() = 0;

protected:
	virtual Status::statusType Initialization() = 0;
	virtual uint32 GetDataRegisterAddress() = 0;

	virtual inline void CallError(Error error) {
		if (onError != nullptr) {
			onError(error);
		}
	}
};
