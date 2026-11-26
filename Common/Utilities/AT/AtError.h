#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/CompileTime/TextLiteral.h>


// The error reporting of the AT library. A message is a template argument, and a failed check calls AtError<Message>(), which is
// declared but never defined. The compiler reports the call as an error and prints the message in the template argument, as the
// command line's CommandError and the FAT12 builder's Fat12Error do.
//
// The error type of an AT call is AtStatus: a ResultStatus, and the CME code when the module answered +CME ERROR: n.


// Declared, never defined. A call from a constant expression is ill-formed, and the diagnostic prints the message.
template<TextLiteral Message>
void AtError();


// Calls AtError<Message>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message>
constexpr void AtRequire(bool condition) {
	if (!condition) {
		AtError<Message>();
	}
}


// The error of an AT call. cmeCode is zero unless status is error and the module sent +CME ERROR: cmeCode. failureCode is zero unless
// status is error and the command has a failure code line (.FailureCode()) that carried a code before the error final. The failure code
// sits next to the status byte, so the status stays 4 bytes and is passed in one register.
struct AtStatus {
	ResultStatus status = ResultStatus::ok;
	uint8 failureCode = 0;
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
