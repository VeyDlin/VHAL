#pragma once
#include <Utilities/DataTypes.h>

namespace VUB::PC {


// How long a release waits for the transfers that are still in flight. None does not wait: the release is finished by a later Finish, from
// Poll. Bounded waits about 200 ms and then leaves the rest to a later Finish. Unbounded waits until every transfer has completed.
enum class ReleaseWait : uint8 {
	None,
	Bounded,
	Unbounded
};


// Closes the handles of a link whose transfers may be in flight. The OS may write into a transfer's buffer until the transfer completes, so
// the handles and the buffers are freed only once no transfer is in flight. Begin cancels the transfers in flight and marks the release as
// closing. Finish frees the handles when nothing is in flight, and otherwise waits as the ReleaseWait allows. An I/O path calls Begin and
// lets Poll call Finish, so it never waits.
//
// Layer is the handle layer of the link. It provides these functions, and none of them waits longer than its milliseconds argument:
//   void CancelAll()                    cancels the transfers in flight; does nothing when none is
//   bool InFlight()                     whether a transfer is still in flight when called
//   void Progress(uint32 milliseconds)  handles the completions that are ready, waiting up to the time given (zero: no wait)
//   void Free()                         frees the handles; called once per release, and only when nothing is in flight
template<typename Layer>
class TransferRelease {
public:
	// The time a bounded wait lasts at most, and the step it takes between checks.
	static constexpr uint32 boundedMs = 200;
	static constexpr uint32 stepMs = 10;


	// Whether a release is in progress: Begin was called and Finish has not yet freed the handles.
	bool Closing() const {
		return closing;
	}


	// Starts the release: cancels the transfers in flight and marks the release as closing. Nothing waits.
	void Begin(Layer& layer) {
		closing = true;
		layer.CancelAll();
	}


	// Frees the handles when no transfer is in flight, waiting as the wait allows. Returns true when the release is done, or when none is in
	// progress. Returns false while a transfer is still in flight; the handles are then kept.
	bool Finish(Layer& layer, ReleaseWait wait) {
		if (!closing) {
			return true;
		}

		layer.Progress(0);
		uint32 waited = 0;
		while (layer.InFlight() && wait != ReleaseWait::None && (wait == ReleaseWait::Unbounded || waited < boundedMs)) {
			layer.Progress(stepMs);
			waited += stepMs;
		}

		if (layer.InFlight()) {
			return false;
		}

		layer.Free();
		closing = false;
		return true;
	}


private:
	bool closing = false;
};

}
