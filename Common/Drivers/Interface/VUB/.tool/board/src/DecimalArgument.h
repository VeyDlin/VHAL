#pragma once
#include <cstdint>
#include <string_view>


namespace VubBoard {
	// Reads a command line number: one or more decimal digits and nothing else, with no sign, no space and no prefix. The value must not be
	// more than maximum. Returns false, and leaves value as it was, for any other text or for a value above maximum. Each digit is checked
	// before it is added, so a value that does not fit the 64-bit type is refused and does not wrap.
	inline bool ParseDecimalArgument(std::string_view text, std::uint64_t maximum, std::uint64_t& value) {
		if (text.empty()) {
			return false;
		}

		std::uint64_t result = 0;
		for (char character : text) {
			if (character < '0' || character > '9') {
				return false;
			}

			std::uint64_t digit = static_cast<std::uint64_t>(character - '0');
			if (digit > maximum || result > (maximum - digit) / 10) {
				return false;
			}

			result = result * 10 + digit;
		}

		value = result;
		return true;
	}


	// The same reading, stored in a number of any integer type that holds maximum. On a refusal the number is left as it was.
	template<typename NumberType>
	bool ParseDecimalInto(std::string_view text, std::uint64_t maximum, NumberType& value) {
		std::uint64_t number = 0;
		if (!ParseDecimalArgument(text, maximum, number)) {
			return false;
		}

		value = static_cast<NumberType>(number);
		return true;
	}
}
