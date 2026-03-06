#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_BOOST_ADAPTER


template<typename HandleType>
class BoostAdapter : public IAdapter {
public:
	struct VoltageSelectOption : IOption<uint8> { 
		using IOption::IOption; 
	};

	struct Parameters {
		VoltageSelectOption voltage;
		bool externalMode = false;
	};


protected:
	HandleType *anacHandle = nullptr;
	Parameters parameters;


public:
	BoostAdapter() = default;
	BoostAdapter(HandleType *anac) : anacHandle(anac) {}
	virtual ~BoostAdapter() = default;

	virtual Status::statusType SetParameters(Parameters val) = 0;
	virtual void Enable() = 0;
	virtual void Disable() = 0;
	virtual void Standby(bool enable) = 0;
	virtual bool IsEnabled() = 0;
};
