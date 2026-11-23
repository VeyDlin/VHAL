#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
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


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32, const VUB::Outbox&) {
	}


	Options options;
};


// The values the constructor of StepFraming was given, so a test can read what the stack's framing got.
inline uint8 testStepDelimiter = 0;


// Framing with a delimiter byte. A frame is the delimiter, the payload and the delimiter again, so the payload must not hold the
// delimiter. Growth is 2. The options give the delimiter, and the Step gives it as a method right after the framing.
struct StepFraming {
	struct Options {
		uint8 delimiter = 0x7E;
	};


	// The option methods of the framing, after it in the chain.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		constexpr Step Delimiter(uint8 value) const {
			Options options = this->LastOptions();
			options.delimiter = value;
			return Step(this->WithLastOptions(options));
		}
	};


	static constexpr bool usesTime = false;
	static constexpr std::size_t MaxBody = 64;


	static constexpr std::size_t Growth(std::size_t length) {
		return length + 2;
	}


	StepFraming() : StepFraming(Options{}) {
	}


	explicit StepFraming(const Options& value) : delimiter(value.delimiter) {
		testStepDelimiter = delimiter;
	}


	Result<std::size_t> Frame(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.size() + 2 > out.size()) {
			return ResultStatus::messageTooLong;
		}

		out[0] = delimiter;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index + 1] = in[index];
		}

		out[in.size() + 1] = delimiter;
		return in.size() + 2;
	}


	VUB::FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (byte != delimiter) {
			if (count < MaxBody) {
				body[count] = byte;
			}

			count++;
			return VUB::FrameStatus::pending;
		}

		std::size_t payload = count;
		count = 0;
		if (payload == 0) {
			return VUB::FrameStatus::pending;
		}

		if (payload > MaxBody || payload > out.size()) {
			return VUB::FrameStatus::tooLong;
		}

		for (std::size_t index = 0; index < payload; index++) {
			out[index] = body[index];
		}

		length = payload;
		return VUB::FrameStatus::packet;
	}


	uint8 delimiter = 0x7E;
	std::array<uint8, MaxBody> body{};
	std::size_t count = 0;
};
