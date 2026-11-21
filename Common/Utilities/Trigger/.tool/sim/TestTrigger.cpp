#include <VHAL.h>
#include <Utilities/Trigger/Trigger.h>
#include <Utilities/Trigger/EdgeTrigger.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <type_traits>


// Host tests for Trigger, EdgeTrigger and the two latch variants. BasicTrigger<true> uses std::atomic and BasicTrigger<false> uses
// a critical section, the path for a target without byte atomics. Both run the same checks. The thread tests put a second thread
// in the role of an interrupt: it sets the latch while the main thread takes it, and every event must arrive exactly once.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestTrigger.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestTrigger.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define TR_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define TR_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// The default alias must pick the atomic latch exactly when the host has lock-free bool atomics. On this host that is the atomic
// branch, so the checks below run it.
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::is_same_v<Trigger, BasicTrigger<true>>);

// Every variant is one latch with one owner, so none of them can be copied, on either branch.
static_assert(!std::is_copy_constructible_v<BasicTrigger<true>>);
static_assert(!std::is_copy_assignable_v<BasicTrigger<true>>);
static_assert(!std::is_copy_constructible_v<BasicTrigger<false>>);
static_assert(!std::is_copy_assignable_v<BasicTrigger<false>>);
static_assert(!std::is_copy_constructible_v<EdgeTrigger>);
static_assert(!std::is_copy_assignable_v<EdgeTrigger>);


template<bool lockFree>
void CheckLatch() {
	BasicTrigger<lockFree> trigger;

	TR_EXPECT(!trigger.IsSet());
	TR_EXPECT(!trigger.Take());

	trigger.Set();
	TR_EXPECT(trigger.IsSet());
	TR_EXPECT(trigger.IsSet());          // reading does not clear the latch
	TR_EXPECT(trigger.Take());
	TR_EXPECT(!trigger.Take());          // the first take cleared it
	TR_EXPECT(!trigger.IsSet());

	// Two sets before one take make one event, as a latch does.
	trigger.Set();
	trigger.Set();
	TR_EXPECT(trigger.Take());
	TR_EXPECT(!trigger.Take());
}


// Set and Take each enter one critical section on the critical-section path. The atomic path takes none.
template<bool lockFree>
void CheckSectionCount() {
	BasicTrigger<lockFree> trigger;

	uint32 mark = simCriticalEnters;
	trigger.Set();
	TR_EQ(simCriticalEnters - mark, lockFree ? 0 : 1);

	mark = simCriticalEnters;
	TR_EXPECT(trigger.Take());
	TR_EQ(simCriticalEnters - mark, lockFree ? 0 : 1);
	TR_EQ(simCriticalDepth, 0);
}


// The producer sets the latch and waits until the consumer has taken it, so every set must come out of Take exactly once.
// The deadline keeps a broken latch from hanging the test.
template<bool lockFree>
void CheckNoLostEvents() {
	constexpr int events = 20000;
	BasicTrigger<lockFree> trigger;
	std::atomic<bool> giveUp = false;
	int produced = 0;

	std::thread interrupt([&]() {
		for (int index = 0; index < events; index++) {
			trigger.Set();
			produced++;
			while (trigger.IsSet() && !giveUp.load()) { }
			if (giveUp.load()) {
				return;
			}
		}
	});

	int taken = 0;
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (taken < events && std::chrono::steady_clock::now() < deadline) {
		if (trigger.Take()) {
			taken++;
		}
	}

	giveUp.store(true);
	interrupt.join();

	TR_EQ(produced, events);
	TR_EQ(taken, events);
	TR_EXPECT(!trigger.IsSet());
}


void CheckEdgeTrigger() {
	EdgeTrigger edge;

	TR_EXPECT(!edge.IsSet());
	TR_EXPECT(!edge.Take());
	TR_EXPECT(!edge.GetState());

	edge.Update(false);
	TR_EXPECT(!edge.IsSet());

	edge.Update(true);
	TR_EXPECT(edge.IsSet());
	TR_EXPECT(edge.GetState());

	edge.Update(true);                   // still high: no new edge, the first one stays latched
	TR_EXPECT(edge.IsSet());
	TR_EXPECT(edge.Take());
	TR_EXPECT(!edge.Take());
	TR_EXPECT(edge.GetState());          // the level is still high after the take

	edge.Update(false);
	TR_EXPECT(!edge.IsSet());
	edge.Update(true);                   // second rising edge
	TR_EXPECT(edge.Take());

	// Reading through a const reference has no side effect.
	const EdgeTrigger& view = edge;
	TR_EXPECT(!view.IsSet());
}


// The producer toggles an EdgeTrigger from the interrupt side, and the main thread takes each rising edge.
void CheckEdgeTriggerNoLostEdges() {
	constexpr int edges = 20000;
	EdgeTrigger edge;
	std::atomic<bool> giveUp = false;
	int produced = 0;

	std::thread interrupt([&]() {
		for (int index = 0; index < edges; index++) {
			edge.Update(true);
			produced++;
			while (edge.IsSet() && !giveUp.load()) { }
			edge.Update(false);
			if (giveUp.load()) {
				return;
			}
		}
	});

	int taken = 0;
	std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
	while (taken < edges && std::chrono::steady_clock::now() < deadline) {
		if (edge.Take()) {
			taken++;
		}
	}

	giveUp.store(true);
	interrupt.join();

	TR_EQ(produced, edges);
	TR_EQ(taken, edges);
}


}


int main() {
	CheckLatch<true>();
	CheckLatch<false>();
	CheckSectionCount<true>();
	CheckSectionCount<false>();
	CheckNoLostEvents<true>();
	CheckNoLostEvents<false>();
	CheckEdgeTrigger();
	CheckEdgeTriggerNoLostEdges();

	if (failureCount == 0) {
		std::printf("TestTrigger: all checks passed\n");
		return 0;
	}

	std::printf("TestTrigger: %d check(s) failed\n", failureCount);
	return 1;
}
