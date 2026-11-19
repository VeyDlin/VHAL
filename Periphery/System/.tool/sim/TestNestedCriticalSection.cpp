#include <System/NestedCriticalSection.h>
#include <cstdio>

// Host tests for the nesting critical section, with a fake interrupt mask in place of PRIMASK. The fake counts the operations,
// so a test sees how many times the mask was read, disabled and enabled, as well as its value.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestNestedCriticalSection.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define NC_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// primask is 1 while the interrupts are masked, as on the Cortex-M PRIMASK register.
uint32 primask = 0;
uint32 readCount = 0;
uint32 disableCount = 0;
uint32 enableCount = 0;


void ResetFake(uint32 initialMask) {
	primask = initialMask;
	readCount = 0;
	disableCount = 0;
	enableCount = 0;
}


struct FakeInterrupts {
	static uint32 ReadMask() {
		readCount++;
		return primask;
	}


	static void Disable() {
		primask = 1;
		disableCount++;
	}


	static void Enable() {
		primask = 0;
		enableCount++;
	}
};


using Section = NestedCriticalSection<FakeInterrupts>;


// One section from interrupts enabled: masked inside, enabled after, one read, one disable and one enable.
void TestSingleSection() {
	ResetFake(0);
	Section::Enter();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(readCount, 1);
	NC_EQ(disableCount, 1);
	NC_EQ(enableCount, 1);
}


// Three nested sections: the inner leaves keep the interrupts masked, and only the outermost leave enables them.
void TestNestedSections() {
	ResetFake(0);
	Section::Enter();
	Section::Enter();
	Section::Enter();
	NC_EQ(primask, 1);

	Section::Leave();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 1);
	NC_EQ(enableCount, 0);

	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 1);
	NC_EQ(disableCount, 3);
}


// Entered with the interrupts already masked (an interrupt-off caller): the outermost leave must leave them masked.
void TestEntryWithInterruptsMasked() {
	ResetFake(1);
	Section::Enter();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 1);
	NC_EQ(enableCount, 0);

	// The same holds with a nested section inside the masked one.
	Section::Enter();
	Section::Enter();
	Section::Leave();
	Section::Leave();
	NC_EQ(primask, 1);
	NC_EQ(enableCount, 0);
}


// The mask is saved at the outermost enter only. An inner enter that finds the interrupts masked by the outer one must not make
// the outermost leave keep them off, because the outer enter found them enabled.
void TestInnerEnterDoesNotOverwriteSavedMask() {
	ResetFake(0);
	Section::Enter();
	Section::Enter();
	Section::Leave();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 1);
}


// A leave with no open section unmasks, as the ports always have, and it does not drive the depth below zero. A section that
// follows still masks and unmasks as usual.
void TestUnbalancedLeave() {
	ResetFake(1);
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 1);

	ResetFake(0);
	Section::Enter();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 1);

	// Two leaves for one enter: the second one has no section to close.
	ResetFake(0);
	Section::Enter();
	Section::Leave();
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 2);

	// The depth stayed at zero, so the next pair is the outermost pair again and masks, then unmasks.
	ResetFake(0);
	Section::Enter();
	NC_EQ(primask, 1);
	Section::Leave();
	NC_EQ(primask, 0);
}


// Code that masks the interrupts while a section is open does not change what the outermost leave does: the mask the outermost
// enter saved decides, here zero, so the leave enables.
void TestOuterSavedMaskDecidesLeave() {
	ResetFake(0);
	Section::Enter();
	// Something outside the section masks the interrupts again while it is open.
	primask = 1;
	Section::Enter();
	Section::Leave();
	NC_EQ(enableCount, 0);
	Section::Leave();
	NC_EQ(primask, 0);
	NC_EQ(enableCount, 1);
}


}


int main() {
	TestSingleSection();
	TestNestedSections();
	TestEntryWithInterruptsMasked();
	TestInnerEnterDoesNotOverwriteSavedMask();
	TestUnbalancedLeave();
	TestOuterSavedMaskDecidesLeave();

	if (failureCount == 0) {
		std::printf("TestNestedCriticalSection: all checks passed\n");
		return 0;
	}

	std::printf("TestNestedCriticalSection: %d check(s) failed\n", failureCount);
	return 1;
}
