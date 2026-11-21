#pragma once
#include "CommandArgument.h"
#include "CommandCheck.h"
#include "CommandModules.h"
#include "CommandNameTree.h"
#include "CommandTypes.h"
#include <Utilities/TextNumber/FloatParser.h>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>


// The FloatArguments module. It adds float and double arguments, read by TextNumber's FloatParser (Common/Utilities/TextNumber):
//
//   using App = CommandLine<AppCommands, AppContext, Help, FloatArguments>;
//
//   .Command("speed", "set the speed", &Speed, Argument<float>("rpm").Range(-1.5f, 2.5f).Default(0.0f))
//   ResultStatus Speed(CommandCall<AppContext> call, float rpm);
//
// Range and Default are checked when the command is declared. A value outside the range is an error at Enter, as an integer's is. Numbers
// take a sign, a point and an exponent. Only finite values are accepted: the bounds of a Range must be finite, and with no Range the
// range is the type's finite range (lowest to max). FloatParser reads inf and nan, and the range check refuses both as out of range,
// since no finite range holds them and no comparison with nan holds.
//
// A float argument is a compile error without this header (the message says so), and without the module listed in the console. The
// header must be included before the command set that uses it. A set with no float argument has no float code, whether or not the
// header is included.
//
// The reader is the parser named by CommandParserOf: CommandFloatParser, which pushes characters through FloatParser. The range is kept by
// the reader, so the parser is the only part that needs TextNumber.


// The parser of a float argument, through the CommandParserOf hook. It takes characters one at a time, undoes the last one, and gives
// the value at the end of the text.
template<std::floating_point T>
class CommandFloatParser {
private:
	FloatParser<T> parser{FloatOptions{}.WithPlusSign()};


public:
	bool Push(char character) {
		return !parser.Push(character).IsError();
	}


	void Pop() {
		parser.Pop();
	}


	bool Finish(T& value) const {
		NumberResult<T> result = parser.Finish();

		if (result.error.IsError()) {
			return false;
		}

		value = result.value;
		return true;
	}
};


template<>
struct CommandParserOf<float> {
	using Type = CommandFloatParser<float>;
};


template<>
struct CommandParserOf<double> {
	using Type = CommandFloatParser<double>;
};


// A float argument. Range sets the accepted values, and must not have its minimum above its maximum. Default makes the argument optional.
template<std::floating_point T>
class Argument<T> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Float;
	using Value = T;

	std::string_view name;
	T minimum = std::numeric_limits<T>::lowest();
	T maximum = std::numeric_limits<T>::max();
	T defaultValue{};
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	constexpr Argument Range(T low, T high) const {
		// A finite value minus itself is zero. Infinity and nan give nan, which is not zero. This needs IEEE arithmetic: under -ffast-math the
		// compiler folds the test to true, so the build must not use it.
		CommandRequire<CommandText{"a float range bound is not finite"}>(low - low == 0 && high - high == 0);
		CommandRequire<CommandText{"a range has its minimum above its maximum"}>(low <= high);

		Argument copy = *this;
		copy.minimum = low;
		copy.maximum = high;
		return copy;
	}


	constexpr Argument Default(T value) const {
		Argument copy = *this;
		copy.defaultValue = value;
		copy.hasDefault = true;
		return copy;
	}


	// Checked when the command is declared, because Range and Default may come in either order.
	constexpr void Verify() const {
		CommandRequire<CommandText{"the default is outside the range of the argument"}>(!hasDefault || (defaultValue >= minimum && defaultValue <= maximum));
	}
};


// The reader of a float argument. It keeps the characters through the parser named by CommandParserOf, and refuses a value outside the range.
template<std::floating_point T>
class CommandFloatSlot final : public CommandSlot {
	static_assert(std::is_trivially_destructible_v<typename CommandParserOf<T>::Type>, "a float argument parser must be trivially destructible");

private:
	typename CommandParserOf<T>::Type parser{};
	T minimum;
	T maximum;
	std::size_t refused = 0;


public:
	explicit CommandFloatSlot(const Argument<T>& declaration) : minimum(declaration.minimum), maximum(declaration.maximum) {
	}


	bool Push(char character) override {
		if (refused == 0 && parser.Push(character)) {
			return true;
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else {
			parser.Pop();
		}
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0) {
			return CommandFault::Invalid;
		}

		T value{};
		if (!parser.Finish(value)) {
			return CommandFault::Invalid;
		}

		if (!(value >= minimum && value <= maximum)) {
			return CommandFault::OutOfRange;
		}

		std::memcpy(destination, &value, sizeof(T));
		return CommandFault::None;
	}
};


template<std::floating_point T>
struct CommandSlotOf<Argument<T>> {
	using Type = CommandFloatSlot<T>;
};


struct FloatArguments {
	using Provides = CommandFloatFeature;
};
