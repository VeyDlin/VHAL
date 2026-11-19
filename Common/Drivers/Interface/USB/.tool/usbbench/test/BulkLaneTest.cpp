#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "BenchProtocol.h"
#include "BulkLane.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


// Drives a lane the way the backends do: retire completed slots, refill free slots, then complete one transfer. The
// newest pending transfer completes first, so completions arrive out of submission order. The device side produces
// the pattern in submission order, the way the device sends one stream.
// Returns false when the lane stalled: nothing in flight although the lane is not finished.
bool RunInLane(BulkLane& lane, std::uint64_t corruptOffset, bool corrupt) {
	BenchProtocol::PatternGenerator device;
	std::vector<BulkTransfer> pending;
	std::uint64_t streamOffset = 0;

	while (true) {
		lane.VerifyCompleted();

		while (lane.CanSubmit()) {
			BulkTransfer transfer;
			if (!lane.BeginSubmit(transfer)) {
				return false;
			}

			for (std::size_t index = 0; index < transfer.length; index++) {
				std::uint8_t value = device.NextByte();
				if (corrupt && streamOffset == corruptOffset) {
					value = static_cast<std::uint8_t>(value ^ 0xFF);
				}

				transfer.data[index] = value;
				streamOffset++;
			}

			pending.push_back(transfer);
		}

		if (lane.IsFinished()) {
			return true;
		}

		if (pending.empty()) {
			return false;
		}

		BulkTransfer done = pending.back();
		pending.pop_back();
		lane.CompleteSubmit(done.slot);
	}
}


// Runs an IN lane and checks the verified byte count and the mismatch report.
void CheckInRun(std::size_t inflight, std::uint64_t total, std::size_t transferSize, bool corrupt) {
	const std::uint64_t corruptOffset = 1000;
	BulkLane lane(BulkDirection::In, total, transferSize, inflight);
	bool finished = RunInLane(lane, corruptOffset, corrupt);

	std::printf(
		"IN  inflight %zu, transfer %zu, total %llu, corrupt %d\n",
		inflight,
		transferSize,
		static_cast<unsigned long long>(total),
		corrupt ? 1 : 0);
	Expect(finished, "IN lane finishes without stalling");
	Expect(lane.GetVerifiedBytes() == total, "IN lane verifies every byte");
	if (corrupt) {
		Expect(lane.GetMismatchBytes() == 1, "IN lane reports the one corrupted byte");
		Expect(lane.GetFirstMismatchOffset() == corruptOffset, "IN lane reports the corrupted offset");
	} else {
		Expect(lane.GetMismatchBytes() == 0, "IN lane reports no mismatch on a clean stream");
	}
}


// Runs an OUT lane: every transfer carries the pattern in submission order, and the lane must finish.
void CheckOutRun(std::size_t inflight, std::uint64_t total, std::size_t transferSize) {
	BulkLane lane(BulkDirection::Out, total, transferSize, inflight);
	BenchProtocol::PatternGenerator expected;
	std::vector<BulkTransfer> pending;
	bool matches = true;
	bool stalled = false;

	while (true) {
		lane.VerifyCompleted();

		while (lane.CanSubmit()) {
			BulkTransfer transfer;
			if (!lane.BeginSubmit(transfer)) {
				stalled = true;
				break;
			}

			for (std::size_t index = 0; index < transfer.length; index++) {
				if (transfer.data[index] != expected.NextByte()) {
					matches = false;
				}
			}

			pending.push_back(transfer);
		}

		if (lane.IsFinished() || stalled) {
			break;
		}

		if (pending.empty()) {
			stalled = true;
			break;
		}

		BulkTransfer done = pending.front();
		pending.erase(pending.begin());
		lane.CompleteSubmit(done.slot);
	}

	std::printf(
		"OUT inflight %zu, transfer %zu, total %llu\n",
		inflight,
		transferSize,
		static_cast<unsigned long long>(total));
	Expect(!stalled, "OUT lane finishes without stalling");
	Expect(matches, "OUT lane transfers carry the pattern in order");
	Expect(lane.GetVerifiedBytes() == total, "OUT lane retires every byte");
}


}


int main() {
	// One transfer in flight: the case that used to stall, because the slot was not reusable until the next pass.
	CheckInRun(1, 2368, 320, false);
	CheckInRun(1, 2368, 320, true);
	CheckInRun(1, 64, 64, false);
	CheckOutRun(1, 2368, 320);

	CheckInRun(2, 2368, 320, false);
	CheckInRun(3, 2368, 64, false);
	CheckInRun(8, 2368, 320, true);
	CheckInRun(8, 65536 * 4, 65536, false);
	CheckOutRun(8, 65536 * 4, 65536);

	std::printf("usbbench lane test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
