#pragma once
#include <Adapter/IWDGAdapter.h>


using AIWDG = class IWDGAdapter001;


class IWDGAdapter001 : public IWDGAdapter<CMSDK_WDT_TypeDef> {
private:
	static constexpr uint32 unlockValue = 0x1ACCE551;
	uint32 loadValue = 0;



public:
	IWDGAdapter001() { }
	IWDGAdapter001(CMSDK_WDT_TypeDef *wdt, uint32 busClockHz = 0):IWDGAdapter(wdt, busClockHz) { }



	virtual void Start() override {
		// Unlock watchdog registers
		iwdgHandle->LOCK = unlockValue;

		// Enable interrupt and reset
		iwdgHandle->CTRL = (1 << 0) | (1 << 1); // INTEN | RESEN

		// Reload
		iwdgHandle->INTCLR = 1;

		// Lock registers
		iwdgHandle->LOCK = 0;
	}



	virtual void Reset() override {
		// Writing to INTCLR clears the interrupt and reloads the counter
		iwdgHandle->LOCK = unlockValue;
		iwdgHandle->INTCLR = 1;
		iwdgHandle->LOCK = 0;
	}



	virtual inline uint32 GetDeadlineMs() const override {
		if (inputBusClockHz == 0) {
			return 0;
		}
		// Counter counts down from LOAD to 0 at busClockHz
		return (uint32)((uint64)loadValue * 1000 / inputBusClockHz);
	}



private:
	inline void OnEnableClock() {
		CMSDK_SYSCON->APB_CLKEN |= (1 << 6); // WDT_PCLK_EN
	}


protected:
	virtual Status::statusType Initialization() override {
		OnEnableClock();

		auto status = BeforeInitialization();
		if (status != Status::ok) {
			return status;
		}

		// Calculate load value from deadline
		status = CalculateDividers();
		if (status != Status::ok) {
			return status;
		}

		// Unlock
		iwdgHandle->LOCK = unlockValue;

		// Write load value
		iwdgHandle->LOAD = loadValue;

		// Set control: enable interrupt and reset
		iwdgHandle->CTRL = (1 << 0) | (1 << 1); // INTEN | RESEN

		// Lock
		iwdgHandle->LOCK = 0;

		return AfterInitialization();
	}



	virtual Status::statusType CalculateDividers() override {
		// WDT is clocked by PCLK (APB clock), fall back to SystemCoreClock
		if (inputBusClockHz == 0) {
			inputBusClockHz = SystemCoreClock;
		}

		// LOAD = (deadlineMs * busClockHz) / 1000
		uint64 val = (uint64)parameters.minDeadlineMs * inputBusClockHz / 1000;

		// Clamp to 32-bit max
		if (val > 0xFFFFFFFF) {
			val = 0xFFFFFFFF;
		}

		loadValue = (uint32)val;

		return Status::ok;
	}

};
