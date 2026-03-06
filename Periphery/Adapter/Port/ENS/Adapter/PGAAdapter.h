#pragma once
#include <Adapter/IAdapter.h>

#define VHAL_PGA_ADAPTER


template<typename HandleType>
class PGAAdapter : public IAdapter {
public:
	struct NegativeInputOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct PositiveInputOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct GainOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct OutputOption : IOption<uint8> {
		using IOption::IOption;
	};

	struct Parameters {
		NegativeInputOption negativeInput;
		PositiveInputOption positiveInput;
		GainOption gain;
		OutputOption output;
	};


protected:
	HandleType *anacHandle = nullptr;
	Parameters parameters;


public:
	PGAAdapter() = default;
	PGAAdapter(HandleType *anac) : anacHandle(anac) {}
	virtual ~PGAAdapter() = default;

	virtual void SetParameters(Parameters val) = 0;
	virtual void Enable() = 0;
	virtual void Disable() = 0;
};
