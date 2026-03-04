#pragma once
#include <Adapter/IAdapter.h>
#include <Adapter/Utilities/IOption.h>


template<typename HandleType>
class PMUAdapter : public IAdapter {
public:
	struct LvdThresholdOption : IOption<uint8> {
		using IOption::IOption;
	};


protected:
	HandleType *anacHandle = nullptr;


public:
	PMUAdapter() = default;
	PMUAdapter(HandleType *anac) : anacHandle(anac) {}
	virtual ~PMUAdapter() = default;

	// LVD
	virtual void SetLvdThreshold(LvdThresholdOption threshold) = 0;
	virtual void EnableLvd(bool enable) = 0;
	virtual bool GetLvdStatus() const = 0;

	// Temperature protection
	virtual void EnableTemperatureProtection(bool enable) = 0;
	virtual bool GetTemperatureAlert() const = 0;

	// Bandgap
	virtual void EnableBandgap(bool enable) = 0;
};
