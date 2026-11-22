#pragma once
#include <Drivers/Interface/Modbus/Pdu/PduCommon.h>
#include <cstddef>
#include <span>


// Function code 10, write multiple registers. The request is the address, the quantity (1 to 123), the byte count, which is twice
// the quantity, and the registers, each big-endian. A normal reply echoes the address and the quantity. No I/O: the caller sends
// the bytes and passes the reply back.
struct WriteMultipleRegistersPdu {
	static constexpr uint8 Code = 0x10;
	static constexpr std::size_t MaxQuantity = 123;
	static constexpr std::size_t RequestHeaderSize = 6;
	static constexpr std::size_t ReplySize = ModbusEchoPduSize;


	// The size of the request PDU for a quantity of registers.
	static constexpr std::size_t RequestSize(std::size_t quantity) {
		return RequestHeaderSize + 2 * quantity;
	}


	// Fails at compile time for a fixed-size write outside the limits.
	template<std::size_t Count>
	static constexpr void RequireCount() {
		static_assert(Count >= 1, "a write needs at least one register");
		static_assert(Count <= MaxQuantity, "FC 10 writes at most 123 registers");
	}


	// Writes the request PDU from the register values. Returns invalidParameter for a count outside 1 to 123 or a pdu too short.
	static constexpr ResultStatus BuildRequest(std::span<uint8> pdu, uint16 address, std::span<const uint16> values) {
		if (!PduQuantityValid(values.size(), MaxQuantity)) {
			return ResultStatus::invalidParameter;
		}

		if (pdu.size() < RequestSize(values.size())) {
			return ResultStatus::invalidParameter;
		}

		pdu[0] = Code;
		PduPutU16(pdu, 1, address);
		PduPutU16(pdu, 3, static_cast<uint16>(values.size()));
		pdu[5] = static_cast<uint8>(2 * values.size());

		for (std::size_t index = 0; index < values.size(); index++) {
			PduPutU16(pdu, RequestHeaderSize + 2 * index, values[index]);
		}

		return ResultStatus::ok;
	}


	// Checks a reply to the request PDU that is sent (checks 2, 3 and 5): the reply must echo the address and the quantity.
	static constexpr ModbusResult CheckReply(std::span<const uint8> request, std::span<const uint8> reply) {
		return PduCheckEcho(request, reply);
	}
};
