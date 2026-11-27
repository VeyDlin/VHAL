#pragma once
#include <cstddef>
#include <string_view>


// A string literal as a class-type template parameter. Compile-time builders take names and messages this way:
// template<TextLiteral Message> accepts "text" in the template argument list, and the type carries the length of the text.
template<std::size_t Size>
struct TextLiteral {
	char value[Size];


	constexpr TextLiteral(const char (&text)[Size]) {
		for (std::size_t index = 0; index < Size; index++) {
			value[index] = text[index];
		}
	}


	// The length without the terminating NUL of the literal.
	constexpr std::size_t GetLength() const {
		return Size - 1;
	}


	constexpr std::string_view View() const {
		return std::string_view(value, Size - 1);
	}
};
