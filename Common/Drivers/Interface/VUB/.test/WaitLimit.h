#pragma once
#include <chrono>


// The limit of the waits of the concurrency tests. A wait for another thread to make progress gives up when this much real time has passed,
// not after a count of rounds, spins or attempts: a thread that the host does not schedule for a while (a loaded runner) then cannot end a
// test that is still making progress. The deadlines of the protocol are on the simulated clock and are not measured here.
namespace VubWait {

inline constexpr std::chrono::seconds Limit{30};


// The end of one wait. It is taken when the wait starts, so each wait has its own 30 seconds.
class WaitLimit {
public:
	WaitLimit()
		: end(std::chrono::steady_clock::now() + Limit) {
	}


	bool Passed() const {
		return std::chrono::steady_clock::now() >= end;
	}


private:
	std::chrono::steady_clock::time_point end;
};

}
