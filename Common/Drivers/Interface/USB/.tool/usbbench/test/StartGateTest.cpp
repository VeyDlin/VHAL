#include <cstddef>
#include <cstdio>
#include <thread>
#include <vector>

#include "StartGate.h"

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


// Every party arrives; each one returns true only once all of them have arrived. The results are kept in ints, not
// bools, because std::vector<bool> packs its elements and threads must not write neighbouring ones.
void CheckReleaseWhenAllArrive() {
	const std::size_t parties = 4;
	StartGate gate(parties);
	std::vector<int> released(parties - 1, 0);
	std::vector<std::thread> threads;
	for (std::size_t index = 0; index + 1 < parties; index++) {
		threads.emplace_back([&gate, &released, index]() {
			released[index] = gate.ArriveAndWait() ? 1 : 0;
		});
	}

	Expect(gate.ArriveAndWait(), "the last party is released when all have arrived");
	for (std::thread& thread : threads) {
		thread.join();
	}

	bool allReleased = true;
	for (int value : released) {
		allReleased = allReleased && value == 1;
	}

	Expect(allReleased, "the waiting parties are released with true");
}


// A cancelled gate releases the party that waits in it, and a party that arrives later does not wait at all.
void CheckCancel() {
	StartGate gate(3);
	int waiterResult = 1;
	std::thread waiter([&gate, &waiterResult]() {
		waiterResult = gate.ArriveAndWait() ? 1 : 0;
	});

	gate.Cancel();
	waiter.join();
	Expect(waiterResult == 0, "a cancelled gate releases a waiting party with false");
	Expect(!gate.ArriveAndWait(), "a party that arrives after the cancel returns false at once");
}


}


int main() {
	CheckReleaseWhenAllArrive();
	CheckCancel();

	std::printf("usbbench start gate test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
