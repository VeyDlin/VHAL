#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The values the constructor of StepLayer was given, so a test can read what the stack's layer got.
inline uint8 testStepFirst = 0;
inline uint8 testStepSecond = 0;


// Layer with two options and a Step: the option methods of the block come right after it in the chain. The layer adds no header and
// no time, so it only carries its options.
struct StepLayer {
	struct Options {
		uint8 first = 1;
		uint8 second = 2;
	};


	// The options of the block, after the block: each method reads them with LastOptions, changes one field, and returns the Step over
	// WithLastOptions.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step First(uint8 value) const {
			Options options = this->LastOptions();
			options.first = value;
			return Step(this->WithLastOptions(options));
		}


		constexpr Step Second(uint8 value) const {
			Options options = this->LastOptions();
			options.second = value;
			return Step(this->WithLastOptions(options));
		}
	};


	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x53544C31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	StepLayer() : StepLayer(Options{}) {
	}


	explicit StepLayer(const Options& value) : options(value) {
		testStepFirst = options.first;
		testStepSecond = options.second;
	}


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8>) {
		return VubTake{0, true};
	}


	void Process(uint32, const VubOutbox&) {
	}


	Options options;
};
