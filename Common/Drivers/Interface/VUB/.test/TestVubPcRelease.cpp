#include "SimCheck.h"
#include <Drivers/Interface/VUB/PC/TransferRelease.h>
#include <Utilities/DataTypes.h>


// The release of a link's transfers, with a fake handle layer: no device is opened. A transfer in flight completes after stepsLeft calls of
// Progress; a negative stepsLeft never completes on its own. A cancelable transfer that never completes on its own completes at the next
// Progress after CancelAll, as a cancelled transfer does. A transfer that is not cancelable ignores CancelAll.


struct FakeLayer {
	int stepsLeft = 0;
	bool cancelable = true;
	int cancels = 0;
	int frees = 0;
	int progressCalls = 0;
	uint32 waitedMs = 0;
	bool freedWhileInFlight = false;


	bool InFlight() const {
		return stepsLeft != 0;
	}


	void CancelAll() {
		cancels++;
		if (stepsLeft < 0 && cancelable) {
			stepsLeft = 1;
		}
	}


	void Progress(uint32 milliseconds) {
		progressCalls++;
		waitedMs += milliseconds;
		if (stepsLeft > 0) {
			stepsLeft--;
		}
	}


	void Free() {
		if (InFlight()) {
			freedWhileInFlight = true;
		}

		frees++;
	}
};


// Nothing in flight: the release finishes at once, without waiting, and frees the handles once.
inline void IdleFinishesAtOnce() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	release.Begin(layer);
	SIM_EXPECT(release.Closing());
	SIM_EXPECT(release.Finish(layer, VUB::PC::ReleaseWait::None));
	SIM_EXPECT(!release.Closing());
	SIM_EXPECT(layer.frees == 1);
	SIM_EXPECT(layer.waitedMs == 0);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// A transfer in flight: Begin cancels it, Finish without a wait does not free the handles and does not wait, and a later Finish frees them
// once the transfer has completed.
inline void InFlightFinishesLater() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	layer.stepsLeft = 3;
	release.Begin(layer);
	SIM_EXPECT(layer.cancels == 1);
	SIM_EXPECT(!release.Finish(layer, VUB::PC::ReleaseWait::None));
	SIM_EXPECT(release.Closing());
	SIM_EXPECT(layer.frees == 0);
	SIM_EXPECT(layer.waitedMs == 0);

	bool done = false;
	for (int call = 0; call < 10 && !done; call++) {
		done = release.Finish(layer, VUB::PC::ReleaseWait::None);
	}

	SIM_EXPECT(done);
	SIM_EXPECT(layer.frees == 1);
	SIM_EXPECT(layer.waitedMs == 0);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// A transfer that never completes, not even when cancelled: a bounded wait gives up after 200 ms, leaves the release closing, and frees
// nothing.
inline void BoundedWaitGivesUp() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	layer.stepsLeft = -1;
	layer.cancelable = false;
	release.Begin(layer);
	SIM_EXPECT(!release.Finish(layer, VUB::PC::ReleaseWait::Bounded));
	SIM_EXPECT(release.Closing());
	SIM_EXPECT(layer.frees == 0);
	SIM_EXPECT(layer.waitedMs == 200);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// A transfer that completes within the bound: the bounded wait returns once it is done, and frees the handles.
inline void BoundedWaitCompletes() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	layer.stepsLeft = 5;
	release.Begin(layer);
	SIM_EXPECT(release.Finish(layer, VUB::PC::ReleaseWait::Bounded));
	SIM_EXPECT(!release.Closing());
	SIM_EXPECT(layer.frees == 1);
	SIM_EXPECT(layer.waitedMs == 40);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// The destructor's release waits past the bound until the transfer completes.
inline void UnboundedWaitCompletes() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	layer.stepsLeft = 1000;
	release.Begin(layer);
	SIM_EXPECT(release.Finish(layer, VUB::PC::ReleaseWait::Unbounded));
	SIM_EXPECT(layer.frees == 1);
	SIM_EXPECT(layer.waitedMs > 200);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// Without the cancel a transfer that never completes keeps the release open. The cancel is what lets the transfer finish.
inline void CancelLetsTransferFinish() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	layer.stepsLeft = -1;
	release.Begin(layer);
	SIM_EXPECT(layer.cancels == 1);
	SIM_EXPECT(release.Finish(layer, VUB::PC::ReleaseWait::None));
	SIM_EXPECT(layer.frees == 1);
	SIM_EXPECT(!layer.freedWhileInFlight);
}


// Finish with no release in progress does nothing.
inline void FinishWithoutBeginDoesNothing() {
	VUB::PC::TransferRelease<FakeLayer> release;
	FakeLayer layer;
	SIM_EXPECT(release.Finish(layer, VUB::PC::ReleaseWait::Unbounded));
	SIM_EXPECT(layer.frees == 0);
	SIM_EXPECT(layer.progressCalls == 0);
}


int main() {
	IdleFinishesAtOnce();
	InFlightFinishesLater();
	BoundedWaitGivesUp();
	BoundedWaitCompletes();
	UnboundedWaitCompletes();
	CancelLetsTransferFinish();
	FinishWithoutBeginDoesNothing();
	return SimFinish("TestVubPcRelease");
}
