#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_LDO_ADAPTER


class LDOAdapter : public IAdapter {
public:
	struct FlagOption : IOptionFlag<uint32> { using IOptionFlag::IOptionFlag; };

	struct Parameters {
		uint8 channelId = 0;
		uint32 voltageMv = 0;
		FlagOption flags;
	};


public:
	LDOAdapter() = default;

	virtual ResultStatus SetParameters(Parameters val) = 0;
	virtual ResultStatus Acquire() = 0;
	virtual ResultStatus Release() = 0;
	virtual ResultStatus SetVoltage(uint32 voltageMv) = 0;


protected:
	Parameters parameters;
};
