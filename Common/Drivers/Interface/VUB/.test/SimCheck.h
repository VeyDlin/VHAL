#pragma once
// The check helpers of the VUB tests, built on the check header of the frame. SIM_EXPECT and SimFinish keep their names, and the
// checks are counted and summarised by VhalCheck.
#include <VhalCheck.h>


// Checks one condition. A failure prints the text of the condition with the line given. This header is the file of a failure.
inline void SimExpect(bool condition, const char* text, int line) {
	VhalCheck::ExpectTrue(condition, __FILE__, line, text);
}


// Checks the condition, and prints its text and line when it fails.
#define SIM_EXPECT(condition) VHAL_CHECK(condition)


// Prints the outcome of the program and returns its exit code.
inline int SimFinish(const char* program) {
	return VhalCheck::Finish(program);
}
