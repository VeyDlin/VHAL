#pragma once
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// Helpers shared by the function code headers of the PDU layer. A PDU is the function code and its data. It has no slave address
// and no CRC, so every length in these headers is a PDU length. The framer adds and checks the address and the CRC.
constexpr std::size_t ModbusMaxPduSize = 253;
constexpr uint8 ModbusExceptionFlag = 0x80;
constexpr std::size_t ModbusExceptionPduSize = 2;
constexpr std::size_t ModbusEchoPduSize = 5;
constexpr std::size_t ModbusReadHeaderSize = 2;

constexpr uint8 ModbusExceptionIllegalFunction = 0x01;
constexpr uint8 ModbusExceptionIllegalDataAddress = 0x02;
constexpr uint8 ModbusExceptionIllegalDataValue = 0x03;
constexpr uint8 ModbusExceptionServerFailure = 0x04;


// Reads a big-endian 16-bit value at index. The caller has checked that two bytes are there.
constexpr uint16 PduGetU16(std::span<const uint8> data, std::size_t index) {
	return static_cast<uint16>((static_cast<uint16>(data[index]) << 8) | data[index + 1]);
}


// Writes a big-endian 16-bit value at index. The caller has checked that two bytes are there.
constexpr void PduPutU16(std::span<uint8> data, std::size_t index, uint16 value) {
	data[index] = static_cast<uint8>(value >> 8);
	data[index + 1] = static_cast<uint8>(value & 0xFF);
}


// The number of bytes that hold count bits, one bit per item, least significant bit first.
constexpr std::size_t PduBitBytes(std::size_t count) {
	return (count + 7) / 8;
}


// A quantity is valid when it is at least one and at most the limit of its function code.
constexpr bool PduQuantityValid(std::size_t quantity, std::size_t limit) {
	return quantity >= 1 && quantity <= limit;
}


// Checks the function code of a reply PDU (check 2) and an exception reply (check 3). Returns ok for a normal reply with the
// code, error with the exception code for an exception reply, and invalidData for any other reply.
constexpr ModbusResult PduCheckFunction(uint8 code, std::span<const uint8> reply) {
	if (reply.empty()) {
		return ModbusResult{ResultStatus::invalidData, 0};
	}

	if (reply[0] == code) {
		return ModbusResult{ResultStatus::ok, 0};
	}

	if (reply[0] == static_cast<uint8>(code | ModbusExceptionFlag)) {
		if (reply.size() != ModbusExceptionPduSize) {
			return ModbusResult{ResultStatus::invalidData, 0};
		}

		return ModbusResult{ResultStatus::error, reply[1]};
	}

	return ModbusResult{ResultStatus::invalidData, 0};
}


// Checks a write reply (checks 2, 3 and 5): a normal reply of five bytes that echoes the address and the value or the quantity of
// the request. The request must hold at least the five bytes of the echo.
constexpr ModbusResult PduCheckEcho(std::span<const uint8> request, std::span<const uint8> reply) {
	if (request.size() < ModbusEchoPduSize) {
		return ModbusResult{ResultStatus::invalidParameter, 0};
	}

	ModbusResult checked = PduCheckFunction(request[0], reply);
	if (checked.status != ResultStatus::ok) {
		return checked;
	}

	if (reply.size() != ModbusEchoPduSize) {
		return ModbusResult{ResultStatus::invalidData, 0};
	}

	for (std::size_t index = 0; index < ModbusEchoPduSize; index++) {
		if (reply[index] != request[index]) {
			return ModbusResult{ResultStatus::invalidData, 0};
		}
	}

	return ModbusResult{ResultStatus::ok, 0};
}
