#pragma once
#include "IAdapter.h"
#include <span>

#define VHAL_RNG_ADAPTER


// True random number generator: a hardware entropy source (STM32 RNG, ESP32 RNG, nRF RNG).
// Read() blocks until the hardware has a word, at most Parameters::timeoutMs. A port whose source can fail at run time
// (the STM32 seed and clock error checks) reports it as an error, never as a weak value.
template<typename HandleType>
class RNGAdapter : public IAdapter {
public:
	struct Parameters {
		// Upper limit for one Read(), in milliseconds. A healthy STM32 RNG gives a word in about 40 RNG clock cycles.
		uint32 timeoutMs = 10;
	};


protected:
	HandleType *rngHandle = nullptr;
	Parameters parameters;


public:
	RNGAdapter() = default;


	RNGAdapter(HandleType *rng) : rngHandle(rng) {
	}


	virtual ResultStatus SetParameters(Parameters val) {
		parameters = val;
		return Initialization();
	}


	const Parameters& GetParameters() const {
		return parameters;
	}


	// One 32-bit random word. Errors:
	//   timeout      no word within Parameters::timeoutMs
	//   noInit       the generator is not running (SetParameters() not called or failed)
	//   invalidData  the hardware reported a seed or clock error; the port has discarded the word and restarted the
	//                source, so the next call may succeed
	virtual Result<uint32> Read() = 0;


	// Fills buffer with random bytes from Read(). Stops at the first error and returns it; the bytes written before it
	// are random, the rest are unchanged.
	ResultStatus Fill(std::span<uint8> buffer) {
		size_t offset = 0;

		while (offset < buffer.size()) {
			Result<uint32> word = Read();
			if (word.IsErr()) {
				return word.Error();
			}

			uint32 value = word.Value();
			for (uint8 i = 0; i < 4 && offset < buffer.size(); i++) {
				buffer[offset] = static_cast<uint8>(value >> (i * 8));
				offset++;
			}
		}

		return ResultStatus::ok;
	}


protected:
	// Enables the source and checks it once. Calls BeforeInitialization() and AfterInitialization().
	virtual ResultStatus Initialization() = 0;
};
