#pragma once
#include "IAdapter.h"

#define AUSED_IWDG_ADAPTER



class IWDGAdapter: public IAdapter {
public:
	struct Parameters {
		uint32 minDeadlineMs = 1000;
	};



protected:
	IWDG_TypeDef *iwdgHandle;
	Parameters parameters;
	uint32 inputBusClockHz = 0;


public:
	IWDGAdapter() { }
	IWDGAdapter(IWDG_TypeDef *iwdg, uint32 busClockHz): iwdgHandle(iwdg), inputBusClockHz(busClockHz) { }



	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}



	virtual void Start() = 0;
	virtual void Reset() = 0;

	virtual inline uint32 GetDeadlineMs() = 0;



protected:
	virtual Status::statusType Initialization() = 0;
	virtual Status::statusType CalculateDividers() = 0;
};















