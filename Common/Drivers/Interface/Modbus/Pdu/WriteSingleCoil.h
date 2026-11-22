#pragma once
#include <Drivers/Interface/Modbus/Pdu/PduCommon.h>
#include <cstddef>
#include <span>


// Function code 05, write single coil. The request is the address and the value: 0xFF00 for on, 0x0000 for off. A normal reply
// echoes the request. No I/O: the caller sends the bytes and passes the reply back.
struct WriteSingleCoilPdu {
	static constexpr uint8 Code = 0x05;
	static constexpr uint16 OnValue = 0xFF00;
	static constexpr uint16 OffValue = 0x0000;
	static constexpr std::size_t RequestSize = ModbusEchoPduSize;
	static constexpr std::size_t ReplySize = ModbusEchoPduSize;


	// Writes the request PDU. Returns invalidParameter for a pdu shorter than RequestSize.
	static constexpr ResultStatus BuildRequest(std::span<uint8> pdu, uint16 address, bool on) {
		if (pdu.size() < RequestSize) {
			return ResultStatus::invalidParameter;
		}

		pdu[0] = Code;
		PduPutU16(pdu, 1, address);
		PduPutU16(pdu, 3, on ? OnValue : OffValue);
		return ResultStatus::ok;
	}


	// Checks a reply to the request PDU that is sent (checks 2, 3 and 5): the reply must echo the request.
	static constexpr ModbusResult CheckReply(std::span<const uint8> request, std::span<const uint8> reply) {
		return PduCheckEcho(request, reply);
	}
};
