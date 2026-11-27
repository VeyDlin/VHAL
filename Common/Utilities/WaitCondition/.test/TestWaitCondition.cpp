#include <VHAL.h>
#include <Utilities/WaitCondition/WaitCondition.h>
#include <cstdio>

// Host tests for WaitCondition::WaitFor. The tick is System::tickLow, which each case sets and moves by hand. A case checks the
// result and the number of predicate calls. The tick reads are inline and cannot be counted, so the tests check the calls the
// wait makes, not the clock loads.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestWaitCondition.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define WC_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// Moves the fake tick forward by one.
void TickOne() {
	System::tickLow = System::tickLow + 1;
}


// Sets the fake tick to a known value and clears the RTOS handle.
void ResetClock(uint32 start) {
	System::tickLow = start;
	System::rtosDelayMsHandle = nullptr;
}


void TestAlreadyTrueReturnsAtOnce() {
	ResetClock(100);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		return true;
	}, 0);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 1);
}


void TestPredicateTrueAfterPolls() {
	ResetClock(500);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		return calls == 5;
	}, 10);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 5);
}


// The tick never moves. The predicate still ends the wait, because the timeout is never reached.
void TestFrozenTickStillReturnsWhenTrue() {
	ResetClock(42);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		return calls == 1000;
	}, 10);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 1000);
	WC_EQ(System::tickLow, 42);
}


// Each predicate call moves the tick by one. The first call starts the clock at tick 1001. The timeout comes after the
// eleventh call, when ten ticks have passed.
void TestTimeoutExactlyAtLimit() {
	ResetClock(1000);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		TickOne();
		return false;
	}, 10);

	WC_EQ(status, ResultStatus::timeout);
	WC_EQ(calls, 11);
}


// A predicate that turns true on the call that reaches the limit still wins over the timeout.
void TestTrueAtLimitWins() {
	ResetClock(1000);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		TickOne();
		return calls == 11;
	}, 10);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 11);
}


void TestZeroTimeout() {
	ResetClock(7);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		return false;
	}, 0);

	WC_EQ(status, ResultStatus::timeout);
	WC_EQ(calls, 2);
}


// The tick starts eight ticks before the 32-bit wrap. The timeout must still come after seventeen calls, as it does without the wrap.
void TestWrapAround() {
	ResetClock(0xFFFFFFF8u);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		TickOne();
		return false;
	}, 16);

	WC_EQ(status, ResultStatus::timeout);
	WC_EQ(calls, 17);
}


void TestWrapAroundTrueBeforeLimit() {
	ResetClock(0xFFFFFFFCu);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		TickOne();
		return calls == 8;
	}, 16);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 8);
}


// The RTOS policy yields one millisecond per poll through the handle. Each yield moves the tick by the delay it was given,
// so the timeout counts milliseconds and the handle is called once per poll.
void TestRtosYieldCountsMilliseconds() {
	ResetClock(0);
	int handleCalls = 0;
	int wrongDelays = 0;

	System::rtosDelayMsHandle = [&](uint32 delay) -> bool {
		handleCalls++;
		if (delay != 1) {
			wrongDelays++;
		}
		System::tickLow = System::tickLow + delay;
		return true;
	};

	ResultStatus status = WaitCondition::WaitFor<WaitCondition::RtosYield>([]() -> bool {
		return false;
	}, 50);

	WC_EQ(status, ResultStatus::timeout);
	WC_EQ(handleCalls, 50);
	WC_EQ(wrongDelays, 0);
}


// Without the policy the default never touches the RTOS handle, even when a handle is set.
void TestDefaultPolicyDoesNotYield() {
	ResetClock(0);
	int handleCalls = 0;

	System::rtosDelayMsHandle = [&](uint32) -> bool {
		handleCalls++;
		return true;
	};

	int calls = 0;
	ResultStatus status = WaitCondition::WaitFor([&]() -> bool {
		calls++;
		return calls == 3;
	}, 100);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(handleCalls, 0);
}


// The RTOS policy with no handle set falls back to polling and still ends on the predicate.
void TestRtosYieldWithoutHandle() {
	ResetClock(0);
	int calls = 0;

	ResultStatus status = WaitCondition::WaitFor<WaitCondition::RtosYield>([&]() -> bool {
		calls++;
		return calls == 3;
	}, 100);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 3);
}


// An lvalue predicate is called in place and is not copied.
void TestLvaluePredicate() {
	ResetClock(0);
	int calls = 0;
	auto predicate = [&]() -> bool {
		calls++;
		return calls == 2;
	};

	ResultStatus status = WaitCondition::WaitFor(predicate, 100);

	WC_EQ(status, ResultStatus::ok);
	WC_EQ(calls, 2);
}


}


int main() {
	TestAlreadyTrueReturnsAtOnce();
	TestPredicateTrueAfterPolls();
	TestFrozenTickStillReturnsWhenTrue();
	TestTimeoutExactlyAtLimit();
	TestTrueAtLimitWins();
	TestZeroTimeout();
	TestWrapAround();
	TestWrapAroundTrueBeforeLimit();
	TestRtosYieldCountsMilliseconds();
	TestDefaultPolicyDoesNotYield();
	TestRtosYieldWithoutHandle();
	TestLvaluePredicate();

	if (failureCount == 0) {
		std::printf("TestWaitCondition: all checks passed\n");
		return 0;
	}

	std::printf("TestWaitCondition: %d check(s) failed\n", failureCount);
	return 1;
}
