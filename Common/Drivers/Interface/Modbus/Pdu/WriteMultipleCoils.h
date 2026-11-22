#pragma once
#include <Drivers/Interface/Modbus/Pdu/PduCommon.h>
#include <cstddef>
#include <span>


// Function code 0F, write multiple coils. The request is the address, the quantity (1 to 1968), the byte count and the coil bits,
// one bit per coil, least significant bit first. A normal reply echoes the address and the quantity. No I/O: the caller sends the
// bytes and passes the reply back.
struct WriteMultipleCoilsPdu {
	static constexpr uint8 Code = 0x0F;
	static constexpr std::size_t MaxQuantity = 1968;
	static constexpr std::size_t RequestHeaderSize = 6;
	static constexpr std::size_t ReplySize = ModbusEchoPduSize;


	// The size of the request PDU for a quantity of coils.
	static constexpr std::size_t RequestSize(std::size_t quantity) {
		return RequestHeaderSize + PduBitBytes(quantity);
	}


	// Fails at compile time for a fixed-size write outside the limits.
	template<std::size_t Count>
	static constexpr void RequireCount() {
		static_assert(Count >= 1, "a write needs at least one coil");
		static_assert(Count <= MaxQuantity, "FC 0F writes at most 1968 coils");
	}


	// Writes the request PDU from the coil values. Returns invalidParameter for a count outside 1 to 1968 or a pdu too short for it.
	static constexpr ResultStatus BuildRequest(std::span<uint8> pdu, uint16 address, std::span<const bool> values) {
		if (!PduQuantityValid(values.size(), MaxQuantity)) {
			return ResultStatus::invalidParameter;
		}

		std::size_t byteCount = PduBitBytes(values.size());
		if (pdu.size() < RequestHeaderSize + byteCount) {
			return ResultStatus::invalidParameter;
		}

		pdu[0] = Code;
		PduPutU16(pdu, 1, address);
		PduPutU16(pdu, 3, static_cast<uint16>(values.size()));
		pdu[5] = static_cast<uint8>(byteCount);

		for (std::size_t index = 0; index < byteCount; index++) {
			pdu[RequestHeaderSize + index] = 0;
		}

		for (std::size_t index = 0; index < values.size(); index++) {
			if (values[index]) {
				pdu[RequestHeaderSize + index / 8] = static_cast<uint8>(pdu[RequestHeaderSize + index / 8] | (1 << (index % 8)));
			}
		}

		return ResultStatus::ok;
	}


	// Checks a reply to the request PDU that is sent (checks 2, 3 and 5): the reply must echo the address and the quantity.
	static constexpr ModbusResult CheckReply(std::span<const uint8> request, std::span<const uint8> reply) {
		return PduCheckEcho(request, reply);
	}
};
