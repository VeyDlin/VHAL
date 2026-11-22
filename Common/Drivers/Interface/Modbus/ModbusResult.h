#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>


// The outcome of one Modbus request. The status is the ResultStatus of the transaction. For an exception reply the status is
// error and exception holds the exception code the slave sent (01, 02, 03, 04 and so on). For every other status exception is 0.
struct ModbusResult {
	ResultStatus status;
	uint8 exception;
};


// A message of a failed compile-time check. Declared, never defined: a call from a constant expression is ill-formed, and the
// diagnostic prints the message in the template argument.
template<TextLiteral Message>
void ModbusFail();


// Calls ModbusFail<Message>() when the condition does not hold. Used only inside constant expressions, in the builders.
template<TextLiteral Message>
constexpr void ModbusRequire(bool condition) {
	if (!condition) {
		ModbusFail<Message>();
	}
}
