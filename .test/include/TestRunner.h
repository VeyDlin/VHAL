#pragma once
// Runs the test cases of a test program. A test case is declared with TEST_CASE. A case stops at its first failed CHECK or
// CHECK_EQ; the checks themselves are the VhalCheck macros, which print each failure with its file and line. A test program calls
// TestRunner::RunAll() and ends with return VhalCheck::Finish("<program name>"). RunAll prints the name of every case that failed.
#include <VhalCheck.h>

#include <cstdio>
#include <string>
#include <vector>


namespace TestRunner {


struct TestEntry {
	std::string name;
	void (*function)();
};


inline std::vector<TestEntry>& Registry() {
	static std::vector<TestEntry> entries;
	return entries;
}


struct Registrar {
	Registrar(const char* name, void (*function)()) {
		Registry().push_back(TestEntry{ name, function });
	}
};


inline void RunAll() {
	for (const TestEntry& entry : Registry()) {
		const int failsBefore = VhalCheck::failCount;
		entry.function();

		if (VhalCheck::failCount != failsBefore) {
			std::printf("failed: %s\n", entry.name.c_str());
		}
	}
}


}


#define CHECK(condition) \
	do { \
		const int failsBefore = VhalCheck::failCount; \
		VHAL_CHECK(condition); \
		if (VhalCheck::failCount != failsBefore) { \
			return; \
		} \
	} while (false)


#define CHECK_EQ(actual, expected) \
	do { \
		const int failsBefore = VhalCheck::failCount; \
		VHAL_CHECK_EQ(actual, expected); \
		if (VhalCheck::failCount != failsBefore) { \
			return; \
		} \
	} while (false)


#define TEST_CASE(testName) \
	static void testName(); \
	static const TestRunner::Registrar testName##Registrar(#testName, &testName); \
	static void testName()
