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
