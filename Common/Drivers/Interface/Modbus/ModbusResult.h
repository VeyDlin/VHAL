#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/CompileTime/Require.h>


// The outcome of one Modbus request. The status is the ResultStatus of the transaction. For an exception reply the status is
// error and exception holds the exception code the slave sent (01, 02, 03, 04 and so on). For every other status exception is 0.
struct ModbusResult {
	ResultStatus status;
	uint8 exception;
};
