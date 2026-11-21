#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

// The start line of a parallel run. Each thread calls ArriveAndWait() once its device is ready. When the last of the
// `parties` arrives, all of them are released together and ArriveAndWait() returns true. Cancel() releases the waiting
// threads at once with false, so a run that could not start every thread never leaves the others waiting.
class StartGate {
private:
	std::mutex mutex;
	std::condition_variable condition;
	std::size_t parties = 0;
	std::size_t arrived = 0;
	bool open = false;
	bool cancelled = false;


public:
	explicit StartGate(std::size_t partyCount) {
		parties = partyCount;
	}


	StartGate(const StartGate&) = delete;
	StartGate& operator=(const StartGate&) = delete;


	// Blocks until every party has arrived or the gate is cancelled. Returns true only when every party arrived.
	bool ArriveAndWait() {
		std::unique_lock<std::mutex> lock(mutex);
		arrived++;
		if (arrived == parties && !cancelled) {
			open = true;
			condition.notify_all();
		}

		condition.wait(lock, [this]() {
			return open || cancelled;
		});
		return open;
	}


	// Releases every waiting party with false. A party that arrives later returns false at once.
	void Cancel() {
		std::lock_guard<std::mutex> lock(mutex);
		cancelled = true;
		condition.notify_all();
	}
};
