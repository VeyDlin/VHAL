#pragma once
#include "../../IWDGAdapter.h"



using AIWDG = class IWDGAdapterF4;


class IWDGAdapterF4: public IWDGAdapter {
private:
	uint32 prescaler = LL_IWDG_PRESCALER_4;
	uint32 prescalerValue = 4;
	uint32 reloadCounter = 4095;




public:
	IWDGAdapterF4() { }
	IWDGAdapterF4(IWDG_TypeDef *iwdg, uint32 busClockHz = 32000):IWDGAdapter(iwdg, busClockHz) { }





	virtual void Start() override {
		LL_IWDG_Enable(iwdgHandle);
		while(LL_IWDG_IsReady(iwdgHandle) != 1);
		Reset();
	}


	virtual void Reset() override {
		LL_IWDG_ReloadCounter(iwdgHandle);
	}


	virtual inline constexpr uint32 GetDeadlineMs() const override {
		return 1000000 / (inputBusClockHz / prescalerValue / reloadCounter);
	}






protected:
	virtual Status::statusType Initialization() override {
		auto status = BeforeInitialization();
		if(status != Status::ok) {
			return status;
		}

		LL_IWDG_EnableWriteAccess(iwdgHandle);
		LL_IWDG_SetPrescaler(iwdgHandle, prescaler);
		LL_IWDG_SetReloadCounter(iwdgHandle, reloadCounter);

		return AfterInitialization();
	}



	virtual Status::statusType CalculateDividers() override {

		return Status::ok;
	}

};















