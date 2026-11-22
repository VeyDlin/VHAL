#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <string_view>


// The error reporting of the AT library. A message is a template argument, and a failed check calls AtError<Message>(), which is
// declared but never defined. The compiler reports the call as an error and prints the message in the template argument, as the
// command line's CommandError and the FAT12 builder's Fat12Error do.
//
// The error type of an AT call is AtStatus: a ResultStatus, and the CME code when the module answered +CME ERROR: n.


// A message, or a name, as a template argument. The terminating zero is not part of the view.
template<std::size_t Size>
struct AtText {
	char value[Size];


	constexpr AtText(const char (&text)[Size]) {
		for (std::size_t index = 0; index < Size; index++) {
			value[index] = text[index];
		}
	}


	constexpr std::string_view View() const {
		return std::string_view(value, Size - 1);
	}
};


// Declared, never defined. A call from a constant expression is ill-formed, and the diagnostic prints the message.
template<AtText Message>
void AtError();


// Calls AtError<Message>() when the condition does not hold. Used only inside constant expressions.
template<AtText Message>
constexpr void AtRequire(bool condition) {
	if (!condition) {
		AtError<Message>();
	}
}


// The error of an AT call. cmeCode is zero unless status is error and the module sent +CME ERROR: cmeCode.
struct AtStatus {
	ResultStatus status = ResultStatus::ok;
	uint16 cmeCode = 0;


	constexpr bool operator==(const AtStatus& other) const = default;
};


// The status of a +CME ERROR: code line.
constexpr AtStatus AtCmeError(uint16 code) {
	AtStatus status;
	status.status = ResultStatus::error;
	status.cmeCode = code;
	return status;
}
