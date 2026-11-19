#include "BulkLane.h"

BulkLane::BulkLane(
	BulkDirection laneDirection,
	std::uint64_t laneTotalBytes,
	std::size_t laneTransferSize,
	std::size_t laneSlotCount)
{
	direction = laneDirection;
	totalBytes = laneTotalBytes;
	transferSize = laneTransferSize;
	slotCount = laneSlotCount == 0 ? 1 : laneSlotCount;
	storage.resize(slotCount * transferSize);
	slotLength.assign(slotCount, 0);
	slotDone.assign(slotCount, false);
	generator.Reset();
}


bool BulkLane::CanSubmit() const {
	if (submittedBytes >= totalBytes) {
		return false;
	}

	return submitSequence - verifySequence < slotCount;
}


bool BulkLane::BeginSubmit(BulkTransfer& transfer) {
	if (!CanSubmit()) {
		return false;
	}

	std::size_t slot = static_cast<std::size_t>(submitSequence % slotCount);
	std::uint64_t remaining = totalBytes - submittedBytes;
	std::size_t length = remaining < transferSize ? static_cast<std::size_t>(remaining) : transferSize;
	std::uint8_t* data = storage.data() + slot * transferSize;

	if (direction == BulkDirection::Out) {
		for (std::size_t index = 0; index < length; index++) {
			data[index] = generator.NextByte();
		}
	}

	slotLength[slot] = length;
	slotDone[slot] = false;
	submittedBytes += length;
	submitSequence++;

	transfer.slot = slot;
	transfer.data = data;
	transfer.length = length;
	return true;
}


void BulkLane::CompleteSubmit(std::size_t slot) {
	slotDone[slot] = true;
}


void BulkLane::VerifyCompleted() {
	while (verifySequence < submitSequence) {
		std::size_t slot = static_cast<std::size_t>(verifySequence % slotCount);
		if (!slotDone[slot]) {
			return;
		}

		std::size_t length = slotLength[slot];
		if (direction == BulkDirection::In) {
			CheckPattern(storage.data() + slot * transferSize, length);
		}

		verifiedBytes += length;
		slotDone[slot] = false;
		verifySequence++;
	}
}


bool BulkLane::IsFinished() const {
	return submittedBytes == totalBytes && verifySequence == submitSequence;
}


std::size_t BulkLane::GetSlotCount() const {
	return slotCount;
}


std::size_t BulkLane::GetSlotLength(std::size_t slot) const {
	return slotLength[slot];
}


std::uint64_t BulkLane::GetVerifiedBytes() const {
	return verifiedBytes;
}


std::uint64_t BulkLane::GetMismatchBytes() const {
	return mismatchBytes;
}


std::uint64_t BulkLane::GetFirstMismatchOffset() const {
	return firstMismatchOffset;
}


void BulkLane::CheckPattern(const std::uint8_t* data, std::size_t length) {
	// The generator keeps running after a mismatch, so one bad byte does not shift the rest of the stream.
	for (std::size_t index = 0; index < length; index++) {
		std::uint8_t expected = generator.NextByte();
		if (data[index] != expected) {
			if (mismatchBytes == 0) {
				firstMismatchOffset = verifiedBytes + index;
			}

			mismatchBytes++;
		}
	}
}
