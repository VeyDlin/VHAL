#pragma once

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>

// Appends printf-style text to a report. Reports are built as strings first, so a failing step never leaves a half
// written report on the console. Lines must stay below the buffer size (512 bytes); longer text is cut.
[[gnu::format(printf, 2, 3)]]
inline void AppendFormat(std::string& text, const char* format, ...) {
	char buffer[512];
	std::va_list args;
	va_start(args, format);
	int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	if (length > 0) {
		std::size_t copied = std::min(static_cast<std::size_t>(length), sizeof(buffer) - 1);
		text.append(buffer, copied);
	}
}


// The IDs, serial numbers and volume labels compare ignoring ASCII case, and the device's text may pad with spaces.
inline std::string UpperAscii(std::string text) {
	for (char& character : text) {
		if (character >= 'a' && character <= 'z') {
			character = static_cast<char>(character - 'a' + 'A');
		}
	}

	return text;
}


inline bool EqualsIgnoreCase(const std::string& first, const std::string& second) {
	return first.size() == second.size() && UpperAscii(first) == UpperAscii(second);
}


inline std::string TrimSpaces(const std::string& text) {
	std::size_t first = text.find_first_not_of(' ');
	if (first == std::string::npos) {
		return "";
	}

	std::size_t last = text.find_last_not_of(' ');
	return text.substr(first, last - first + 1);
}
