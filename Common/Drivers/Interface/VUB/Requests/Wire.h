#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>


namespace VUB {


// The wire values of the requests layer. Kinds are byte 0 of a packet; codes are the FINAL code byte. The layer's packets are the payloads
// of the layer below: a request is 01 id number(2) tag(1) body, an interim is 02 number(2) tag(1) index(1) status(1), and a final is
// 03 number(2) tag(1) code(1), with status(1) and body after code 0.
namespace RequestWire {
	inline constexpr uint8 KindRequest = 0x01;
	inline constexpr uint8 KindInterim = 0x02;
	inline constexpr uint8 KindFinal = 0x03;
	inline constexpr uint8 KindEvent = 0x04;
	inline constexpr uint8 KindCancel = 0x05;
	inline constexpr std::size_t RequestHeader = 5;
	inline constexpr std::size_t InterimSize = 6;
	inline constexpr std::size_t FinalHeader = 6;
	inline constexpr std::size_t FinalErrorSize = 5;
	inline constexpr std::size_t EventHeader = 2;
	inline constexpr std::size_t CancelSize = 4;
	inline constexpr uint8 CodeOk = 0;
	inline constexpr uint8 CodeCancelled = 1;
	inline constexpr uint8 CodeNotSupported = 2;
	inline constexpr uint8 CodeInvalidData = 3;
	inline constexpr uint8 CodeBusy = 4;
	inline constexpr uint8 CodeTimeout = 5;
	inline constexpr uint8 CodeInvalidParameter = 6;
	inline constexpr uint8 StatusCancelRefused = 0xF8;
	inline constexpr uint8 StatusReservedFrom = 0xF0;


	// The result of a final code. Codes 7 to 255 are not used by version 1, and a requester refuses them as invalidData.
	inline ResultStatus ResultOfCode(uint8 code) {
		if (code == CodeCancelled) {
			return ResultStatus::operationCancelled;
		}

		if (code == CodeNotSupported) {
			return ResultStatus::notSupported;
		}

		if (code == CodeBusy) {
			return ResultStatus::busy;
		}

		if (code == CodeTimeout) {
			return ResultStatus::timeout;
		}

		if (code == CodeInvalidParameter) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::invalidData;
	}


	// True when the clock has reached due. The difference is a signed 32-bit count of microseconds, so a clock that wraps still compares right.
	inline bool Due(uint32 nowUs, uint32 dueUs) {
		return static_cast<int32>(nowUs - dueUs) >= 0;
	}
}

}
