#pragma once
#include <Drivers/Interface/Modbus/Pdu/PduCommon.h>
#include <cstddef>
#include <span>


// Function code 03, read holding registers. The request is the address and the quantity, 1 to 125 registers. The reply is the byte
// count, which is twice the quantity, and the registers, each big-endian. No I/O: the caller sends the bytes and passes the reply
// back.
struct ReadHoldingRegistersPdu {
	static constexpr uint8 Code = 0x03;
	static constexpr uint16 MaxQuantity = 125;
	static constexpr std::size_t RequestSize = 5;


	// The size of the reply PDU for a quantity of registers.
	static constexpr std::size_t ReplySize(uint16 quantity) {
		return ModbusReadHeaderSize + 2 * static_cast<std::size_t>(quantity);
	}


	// Fails at compile time for a fixed-size read outside the limits.
	template<std::size_t Count>
	static constexpr void RequireCount() {
		static_assert(Count >= 1, "a read needs at least one register");
		static_assert(Count <= MaxQuantity, "FC 03 reads at most 125 registers");
	}


	// Writes the request PDU. Returns invalidParameter for a quantity outside 1 to 125 or a pdu shorter than RequestSize.
	static constexpr ResultStatus BuildRequest(std::span<uint8> pdu, uint16 address, uint16 quantity) {
		if (!PduQuantityValid(quantity, MaxQuantity) || pdu.size() < RequestSize) {
			return ResultStatus::invalidParameter;
		}

		pdu[0] = Code;
		PduPutU16(pdu, 1, address);
		PduPutU16(pdu, 3, quantity);
		return ResultStatus::ok;
	}


	// Checks a reply to the request PDU that is sent (checks 2, 3 and 4). The byte count must be twice the quantity.
	static constexpr ModbusResult CheckReply(std::span<const uint8> request, std::span<const uint8> reply) {
		if (request.size() != RequestSize) {
			return ModbusResult{ResultStatus::invalidParameter, 0};
		}

		uint16 quantity = PduGetU16(request, 3);
		if (!PduQuantityValid(quantity, MaxQuantity)) {
			return ModbusResult{ResultStatus::invalidParameter, 0};
		}

		ModbusResult checked = PduCheckFunction(Code, reply);
		if (checked.status != ResultStatus::ok) {
			return checked;
		}

		std::size_t byteCount = 2 * static_cast<std::size_t>(quantity);
		if (reply.size() != ModbusReadHeaderSize + byteCount || static_cast<std::size_t>(reply[1]) != byteCount) {
			return ModbusResult{ResultStatus::invalidData, 0};
		}

		return ModbusResult{ResultStatus::ok, 0};
	}


	// Copies the registers of a checked reply into values. The size of values is the requested quantity.
	static constexpr void ReadValues(std::span<const uint8> reply, std::span<uint16> values) {
		for (std::size_t index = 0; index < values.size(); index++) {
			values[index] = PduGetU16(reply, ModbusReadHeaderSize + 2 * index);
		}
	}
};
