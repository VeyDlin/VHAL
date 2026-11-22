#pragma once
#include "AtError.h"
#include <Utilities/CommandLine/CommandTypes.h>
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextNumber/IntegerFormat.h>
#include <Utilities/TextNumber/IntegerParser.h>
#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>


// The value grammar of AT, shared by the server and the client. A value is one field of a line:
//   int8, int16, int32, int64   signed decimal, read by IntegerParser
//   uint8, uint16, uint32       unsigned decimal
//   bool                        0 or 1 only, never read as a number
//   String<N>                   quoted text of at most N characters; \" and \\ are the escapes
// An optional argument has no text when its field is empty. That rule belongs to the field splitter, not to a value.
//
// A reader takes a value one character at a time (Push), then gives the value or a status (Finish). Once Push refuses a character,
// every later character is refused too, so Finish reports the whole field. The readers are plain structs with no virtual calls.
// A writer prints a value with the TextNumber formatter, or with the quoting rule for a string.


// The types the grammar carries. A String is supported at any capacity above zero (AtCapacityOf).
template<typename T>
inline constexpr bool AtIsSupported = std::is_same_v<T, int8> || std::is_same_v<T, int16> || std::is_same_v<T, int32> ||
	std::is_same_v<T, int64> || std::is_same_v<T, uint8> || std::is_same_v<T, uint16> || std::is_same_v<T, uint32> ||
	std::is_same_v<T, bool>;


template<std::size_t Capacity>
inline constexpr bool AtIsSupported<String<Capacity>> = true;


// True for a string type, which has a capacity.
template<typename T>
inline constexpr bool AtIsStringType = false;


template<std::size_t Capacity>
inline constexpr bool AtIsStringType<String<Capacity>> = true;


// The capacity of a string type, and 1 for any other type, so that a zero capacity is the only string error.
template<typename T>
inline constexpr std::size_t AtCapacityOf = 1;


template<std::size_t Capacity>
inline constexpr std::size_t AtCapacityOf<String<Capacity>> = Capacity;


// The most characters a value of type T takes on the wire, without the field separators. A string counts two quotes and an escape for
// each character.
template<typename T>
inline constexpr std::size_t AtValueChars = std::numeric_limits<T>::digits10 + 1 + (std::is_signed_v<T> ? 1 : 0);


template<>
inline constexpr std::size_t AtValueChars<bool> = 1;


template<std::size_t Capacity>
inline constexpr std::size_t AtValueChars<String<Capacity>> = 2 + 2 * Capacity;


// Reads an integer. The parser's options are the type's own range, with a minus sign for a signed type and no plus sign.
template<typename T>
class AtIntegerReader {
private:
	IntegerParser<T> parser{};
	bool refused = false;


public:
	constexpr bool Push(char character) {
		if (refused) {
			return false;
		}

		if (parser.Push(character).IsError()) {
			refused = true;
			return false;
		}

		return true;
	}


	constexpr ResultStatus Finish(T& destination) const {
		if (refused) {
			return ResultStatus::invalidData;
		}

		NumberResult<T> result = parser.Finish();

		if (result.error.code == NumberErrorCode::OutOfRange) {
			return ResultStatus::outOfRange;
		}

		if (result.error.IsError()) {
			return ResultStatus::invalidData;
		}

		destination = result.value;
		return ResultStatus::ok;
	}


	// An integer is never quoted, so a separator or a chain character always ends its field.
	constexpr bool Quoting() const {
		return false;
	}
};


// Reads a bool: one character, 0 or 1.
class AtBoolReader {
private:
	bool filled = false;
	bool value = false;
	bool refused = false;


public:
	constexpr bool Push(char character) {
		if (filled || refused || (character != '0' && character != '1')) {
			refused = true;
			return false;
		}

		value = character == '1';
		filled = true;
		return true;
	}


	constexpr ResultStatus Finish(bool& destination) const {
		if (refused || !filled) {
			return ResultStatus::invalidData;
		}

		destination = value;
		return ResultStatus::ok;
	}


	constexpr bool Quoting() const {
		return false;
	}
};


// Reads a quoted string. The opening quote comes first, then the characters, then the closing quote. Nothing may follow the closing
// quote. A backslash escapes the next character, which must be a quote or a backslash.
template<std::size_t Capacity>
class AtStringReader {
private:
	enum class State : uint8 {
		opening,
		text,
		escape,
		closed,
		refused
	};


	State state = State::opening;
	String<Capacity> text{};


public:
	constexpr bool Push(char character) {
		if (state == State::opening) {
			if (character == '"') {
				state = State::text;
				return true;
			}
		} else if (state == State::text) {
			if (character == '\\') {
				state = State::escape;
				return true;
			}

			if (character == '"') {
				state = State::closed;
				return true;
			}

			return Append(character);
		} else if (state == State::escape) {
			if (character == '"' || character == '\\') {
				state = State::text;
				return Append(character);
			}
		}

		state = State::refused;
		return false;
	}


	constexpr ResultStatus Finish(String<Capacity>& destination) const {
		if (state != State::closed) {
			return ResultStatus::invalidData;
		}

		destination = text;
		return ResultStatus::ok;
	}


	// Inside the quotes a separator, a comma or a semicolon is text, and the field splitter must not read it as one.
	constexpr bool Quoting() const {
		return state == State::text || state == State::escape;
	}


private:
	constexpr bool Append(char character) {
		if (text.length >= Capacity) {
			state = State::refused;
			return false;
		}

		text.characters[text.length] = character;
		text.length++;
		return true;
	}
};


// The reader of a type: the integer reader, except for bool and String.
template<typename T>
struct AtReaderOf {
	using Type = AtIntegerReader<T>;
};


template<>
struct AtReaderOf<bool> {
	using Type = AtBoolReader;
};


template<std::size_t Capacity>
struct AtReaderOf<String<Capacity>> {
	using Type = AtStringReader<Capacity>;
};


template<typename T>
using AtValueReader = typename AtReaderOf<T>::Type;


// Prints an integer in decimal. Returns the number of characters written. The sink is any type with Write(std::string_view): a Print
// for the server, and a text buffer for the client, which encodes its requests before it queues them.
template<typename Sink, typename T>
requires (std::is_integral_v<T> && !std::is_same_v<T, bool>)
std::size_t AtWriteValue(Sink& out, T value) {
	std::array<char, 24> digits{};
	std::size_t length = FormatInteger(std::span<char>(digits), value);
	return out.Write(std::string_view(digits.data(), length));
}


// Prints a bool as 0 or 1.
template<typename Sink>
std::size_t AtWriteValue(Sink& out, bool value) {
	return out.Write(std::string_view(value ? "1" : "0"));
}


// Prints text in quotes, with \" and \\ for a quote and a backslash.
template<typename Sink>
std::size_t AtWriteQuoted(Sink& out, std::string_view text) {
	std::size_t written = out.Write(std::string_view("\""));

	for (std::size_t index = 0; index < text.size(); index++) {
		char character = text[index];

		if (character == '"' || character == '\\') {
			written += out.Write(std::string_view("\\"));
		}

		written += out.Write(std::string_view(&text[index], 1));
	}

	written += out.Write(std::string_view("\""));
	return written;
}


// Prints a string value in quotes.
template<typename Sink, std::size_t Capacity>
std::size_t AtWriteValue(Sink& out, const String<Capacity>& value) {
	return AtWriteQuoted(out, value.View());
}


// Prints the range of an argument in a test reply: the declared minimum and maximum for an integer, (0,1) for a bool and (0-N) for a
// string of capacity N. The caller writes the parentheses.
template<typename T>
std::size_t AtWriteRange(Print& out, int64 minimum, int64 maximum) {
	if constexpr (std::is_same_v<T, bool>) {
		return out.Write(std::string_view("0,1"));
	} else if constexpr (std::is_integral_v<T>) {
		std::size_t written = AtWriteValue(out, minimum);
		written += out.Write(std::string_view("-"));
		written += AtWriteValue(out, maximum);
		return written;
	} else {
		std::size_t written = out.Write(std::string_view("0-"));
		written += AtWriteValue(out, static_cast<uint32>(AtCapacityOf<T>));
		return written;
	}
}
