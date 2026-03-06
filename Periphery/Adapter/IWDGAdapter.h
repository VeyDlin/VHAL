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



	virtual Status::statusType SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}



	virtual void Start() = 0;
	virtual void Reset() = 0;

	virtual inline uint32 GetDeadlineMs() const = 0;



protected:
	virtual Status::statusType Initialization() = 0;
	virtual Status::statusType CalculateDividers() = 0;
};















