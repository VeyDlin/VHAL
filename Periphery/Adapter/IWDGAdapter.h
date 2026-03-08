#pragma once
#include "IAdapter.h"

#define VHAL_IWDG_ADAPTER


template<typename HandleType>
class IWDGAdapter: public IAdapter {
public:
	struct Parameters {
		uint32 minDeadlineMs = 1000;
	};



protected:
	HandleType *iwdgHandle;
	Parameters parameters;
	uint32 inputBusClockHz = 0;


public:
	IWDGAdapter() = default;
	IWDGAdapter(HandleType *iwdg, uint32 busClockHz): iwdgHandle(iwdg), inputBusClockHz(busClockHz) { }



	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}



	virtual void Start() = 0;
	virtual void Reset() = 0;

	virtual inline uint32 GetDeadlineMs() const = 0;



protected:
	virtual ResultStatus Initialization() = 0;
	virtual ResultStatus CalculateDividers() = 0;
};















