#pragma once
// The check helpers of the VUB tests, built on the check header of the frame. SIM_EXPECT and SimFinish keep their names, and the
// checks are counted and summarised by VhalCheck.
#include <VhalCheck.h>


// Checks one condition at the line given. A failure prints the file of the call, the line, and the text of the condition. The macro
// keeps the file of the caller: a function would report this header.
#define SimExpect(condition, text, line) ::VhalCheck::ExpectTrue(static_cast<bool>(condition), __FILE__, line, text)


// Checks the condition, and prints its text and line when it fails.
#define SIM_EXPECT(condition) VHAL_CHECK(condition)


// Prints the outcome of the program and returns its exit code.
inline int SimFinish(const char* program) {
	return VhalCheck::Finish(program);
}
