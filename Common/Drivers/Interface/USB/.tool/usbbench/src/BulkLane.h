#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BenchProtocol.h"

enum class BulkDirection : std::uint8_t {
	In,
	Out
};

// One transfer handed to a backend. For IN the backend fills `data`; for OUT it sends `data`.
struct BulkTransfer {
	std::size_t slot = 0;
	std::uint8_t* data = nullptr;
	std::size_t length = 0;
};

// One direction of a benchmark run: a byte budget, a fixed set of transfer slots, and the pattern check.
//
// Transfers may complete in any order the backend reports them, but they are verified strictly in submission order.
// The pattern is one byte stream, so the check has to see the bytes in the order the device sent them.
class BulkLane {
private:
	BulkDirection direction = BulkDirection::In;
	std::uint64_t totalBytes = 0;
	std::size_t transferSize = 0;
	std::size_t slotCount = 1;
	std::uint64_t submittedBytes = 0;
	std::uint64_t submitSequence = 0;
	std::uint64_t verifySequence = 0;
	std::uint64_t verifiedBytes = 0;
	std::uint64_t mismatchBytes = 0;
	std::uint64_t firstMismatchOffset = 0;
	BenchProtocol::PatternGenerator generator;
	std::vector<std::uint8_t> storage;
	std::vector<std::size_t> slotLength;
	std::vector<bool> slotDone;


public:
	BulkLane(
		BulkDirection laneDirection,
		std::uint64_t laneTotalBytes,
		std::size_t laneTransferSize,
		std::size_t laneSlotCount);


	// True when another transfer may be started: bytes remain and a slot is free. A slot is free again only after
	// VerifyCompleted() has retired it, so a backend verifies before it submits.
	bool CanSubmit() const;


	// Takes the next slot for a transfer. For OUT the pattern is written into the slot here, in submission order.
	bool BeginSubmit(BulkTransfer& transfer);


	// Marks a slot as completed by the backend. The backend has already checked the transferred length.
	void CompleteSubmit(std::size_t slot);


	// Checks and retires completed slots from the oldest one on; stops at the first slot still in flight.
	void VerifyCompleted();


	bool IsFinished() const;


	std::size_t GetSlotCount() const;


	std::size_t GetSlotLength(std::size_t slot) const;


	std::uint64_t GetVerifiedBytes() const;


	std::uint64_t GetMismatchBytes() const;


	std::uint64_t GetFirstMismatchOffset() const;


private:
	void CheckPattern(const std::uint8_t* data, std::size_t length);
};
