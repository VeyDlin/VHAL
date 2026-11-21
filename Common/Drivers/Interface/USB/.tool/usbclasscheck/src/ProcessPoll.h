#pragma once

// The decision of the loop that waits for a program and reads its output. The Win32 code reads the facts (the program has ended, output is
// waiting, the deadline has passed) and this function says what the loop does next. It has no Win32 calls, so the tests run without a board.

enum class PollStep {
	// The program has ended and its output is drained.
	Finish,
	// The deadline has passed. The program is ended.
	Timeout,
	// Output is waiting. It is read, and the loop starts again.
	ReadOutput,
	// Nothing is waiting. The loop sleeps briefly and starts again.
	Wait
};


// The deadline is checked before any read, so a program that keeps writing output cannot keep the loop past its deadline. A program that
// has ended with output still waiting is read to its end first, but it is still timed out when the deadline has passed.
PollStep DecidePollStep(bool exited, bool outputReady, bool deadlinePassed);
