#pragma once
#include "SimCheck.h"
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// Helpers of the link tests: a clock, a record of what a receiver was given, a log of the payloads a stack hands up, and payloads
// with zero bytes in them, so the framings see them.


// The clock of the link tests. Tests set linkNowUs.
inline uint32 linkNowUs = 0;


inline uint32 LinkClock() {
	return linkNowUs;
}


// What a receiver was given: the bytes of a byte-stream link, the packets of a packet link, and the room it reports.
struct FeedRecord {
	std::array<uint8, 512> bytes{};
	std::size_t byteCount = 0;
	std::size_t packetCount = 0;
	std::array<std::size_t, 16> packetLengths{};
	std::size_t room = 256;
	std::size_t roomCalls = 0;
};


inline void FeedBytesIntoRecord(void* owner, std::span<const uint8> bytes) {
	FeedRecord* record = static_cast<FeedRecord*>(owner);
	for (std::size_t index = 0; index < bytes.size(); index++) {
		if (record->byteCount < record->bytes.size()) {
			record->bytes[record->byteCount] = bytes[index];
		}

		record->byteCount++;
	}
}


inline void FeedPacketIntoRecord(void* owner, std::span<const uint8> packet) {
	FeedRecord* record = static_cast<FeedRecord*>(owner);
	if (record->packetCount < record->packetLengths.size()) {
		record->packetLengths[record->packetCount] = packet.size();
	}

	record->packetCount++;
}


inline std::size_t RoomOfRecord(void* owner) {
	FeedRecord* record = static_cast<FeedRecord*>(owner);
	record->roomCalls++;
	return record->room;
}


// A receiver that writes into record.
inline VUB::Receiver RecordReceiver(FeedRecord& record) {
	return VUB::Receiver(&record, &FeedBytesIntoRecord, &FeedPacketIntoRecord, &RoomOfRecord);
}


// The payloads a stack handed up, in order: up to 32 packets of up to 64 bytes.
struct PayloadLog {
	std::array<std::array<uint8, 64>, 32> packets{};
	std::array<std::size_t, 32> lengths{};
	std::size_t count = 0;


	void Add(std::span<const uint8> payload) {
		if (count < packets.size()) {
			for (std::size_t index = 0; index < payload.size() && index < 64; index++) {
				packets[count][index] = payload[index];
			}

			lengths[count] = payload.size();
		}

		count++;
	}


	// True when the index-th payload is exactly the expected bytes.
	bool Is(std::size_t index, std::span<const uint8> expected) const {
		if (index >= count || index >= packets.size() || lengths[index] != expected.size()) {
			return false;
		}

		for (std::size_t byteIndex = 0; byteIndex < expected.size(); byteIndex++) {
			if (packets[index][byteIndex] != expected[byteIndex]) {
				return false;
			}
		}

		return true;
	}
};


inline PayloadLog logAtA;
inline PayloadLog logAtB;


inline void ReceiveAtA(const VUB::Meta&, std::span<const uint8> payload) {
	logAtA.Add(payload);
}


inline void ReceiveAtB(const VUB::Meta&, std::span<const uint8> payload) {
	logAtB.Add(payload);
}


// A payload of 24 bytes. Every fifth byte is zero, and the rest count up from id.
inline std::array<uint8, 24> LinkPayload(uint8 id) {
	std::array<uint8, 24> bytes{};
	for (std::size_t index = 0; index < bytes.size(); index++) {
		if (index % 5 == 4) {
			bytes[index] = 0;
		} else {
			bytes[index] = static_cast<uint8>(id + index);
		}
	}

	return bytes;
}


// Clears what the tests keep between cases.
inline void ResetLinkKit() {
	linkNowUs = 0;
	logAtA.count = 0;
	logAtB.count = 0;
}
