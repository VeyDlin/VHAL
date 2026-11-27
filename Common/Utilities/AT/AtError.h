#pragma once
#include <Utilities/CompileTime/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>


// The error type of an AT call is AtStatus: a ResultStatus, and the CME code when the module answered +CME ERROR: n. The compile-time
// checks of the AT library are Require checks whose messages start with "AT: " (Utilities/CompileTime/Require.h).


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
