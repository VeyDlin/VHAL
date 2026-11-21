#include <cstdio>

#include "ProcessPoll.h"

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


// A program that has ended with its output drained stops the loop, before the deadline or after it.
void CheckFinished() {
	Expect(DecidePollStep(true, false, false) == PollStep::Finish, "an ended program with no output left finishes");
	Expect(DecidePollStep(true, false, true) == PollStep::Finish, "an ended program with no output left finishes even past the deadline");
}


// An ended program's output is read to its end, as long as the deadline has not passed.
void CheckEndedWithOutput() {
	Expect(DecidePollStep(true, true, false) == PollStep::ReadOutput, "an ended program's waiting output is read");
	Expect(DecidePollStep(true, true, true) == PollStep::Timeout, "an ended program's output still flowing past the deadline times out");
}


// A running program that keeps writing is read while the deadline is open, and ended once it has passed. The second case is the one that
// used to escape: the read skipped the deadline check.
void CheckRunningWithOutput() {
	Expect(DecidePollStep(false, true, false) == PollStep::ReadOutput, "a running program's waiting output is read before the deadline");
	Expect(DecidePollStep(false, true, true) == PollStep::Timeout, "a running program that keeps writing is timed out at the deadline");
}


void CheckRunningQuiet() {
	Expect(DecidePollStep(false, false, false) == PollStep::Wait, "a quiet running program is waited for before the deadline");
	Expect(DecidePollStep(false, false, true) == PollStep::Timeout, "a quiet running program is timed out at the deadline");
}


}


int main() {
	CheckFinished();
	CheckEndedWithOutput();
	CheckRunningWithOutput();
	CheckRunningQuiet();

	std::printf("usbclasscheck process poll test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
