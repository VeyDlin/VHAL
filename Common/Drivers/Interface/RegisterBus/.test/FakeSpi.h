#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <vector>


// A fake SPI adapter with the public members SpiRegisterBus uses: Continuous, WriteArray and ReadArray. It logs chip-select
// windows and the bytes in each, in order. Reads return the bytes queued in reply, and 0xFF past its end.


enum class FakeSpiKind {
	Select,
	Release,
	Write,
	Read
};


struct FakeSpiEvent {
	FakeSpiKind kind;
	std::vector<uint8> bytes;
};


struct FakeSpiAdapter {
	std::vector<FakeSpiEvent> events;
	std::vector<uint8> reply;
	std::size_t replyIndex = 0;
	bool continuous = false;


	void Continuous(bool enable) {
		if (enable) {
			events.push_back(FakeSpiEvent{ FakeSpiKind::Select, std::vector<uint8>() });
		} else {
			events.push_back(FakeSpiEvent{ FakeSpiKind::Release, std::vector<uint8>() });
		}
		continuous = enable;
	}


	template<typename DataType>
	ResultStatus WriteArray(const DataType* buffer, uint32 size) {
		FakeSpiEvent event{ FakeSpiKind::Write, std::vector<uint8>() };
		for (uint32 index = 0; index < size; index++) {
			event.bytes.push_back(static_cast<uint8>(buffer[index]));
		}
		events.push_back(event);
		return ResultStatus::ok;
	}


	template<typename DataType>
	ResultStatus ReadArray(DataType* buffer, uint32 size) {
		FakeSpiEvent event{ FakeSpiKind::Read, std::vector<uint8>() };
		for (uint32 index = 0; index < size; index++) {
			uint8 value = replyIndex < reply.size() ? reply[replyIndex] : 0xFF;
			replyIndex++;
			buffer[index] = value;
			event.bytes.push_back(value);
		}
		events.push_back(event);
		return ResultStatus::ok;
	}
};
