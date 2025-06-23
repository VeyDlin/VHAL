#pragma once
#include "IAdapter.h"

#define AUSED_DAC_ADAPTER



class DACAdapter: public IAdapter {
public:
	enum class OutputBuffer { Enable, Disable };

	struct Parameters {
		OutputBuffer outputBuffer = OutputBuffer::Enable;
	};


protected:
	DAC_TypeDef *dacHandle;
	Parameters parameters;
	uint8 dacChannel = 1;



public:
	DACAdapter() = default;
	DACAdapter(DAC_TypeDef *dac, uint8 channel):dacHandle(dac), dacChannel(channel) { }


	virtual inline void IrqHandler() = 0;


	virtual Status::statusType Write(uint16 val) = 0;


	virtual void SetParameters(Parameters val) {
		parameters = val;
		Initialization();
		Enable();
	}


	virtual Status::statusType Enable() = 0;
	virtual Status::statusType Disable() = 0;

protected:
	virtual Status::statusType Initialization() = 0;
};















