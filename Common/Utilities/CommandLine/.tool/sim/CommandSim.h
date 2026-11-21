#pragma once
#include <Utilities/Console/Print.h>
#include <cstdio>
#include <string>


// The host test helpers of the command line: a check that counts failures without a framework, and a Print that keeps what it is
// given. A test executable ends with CommandSim::Finish, which returns non-zero when a check failed.
namespace CommandSim {


inline int failureCount = 0;


inline void Expect(bool condition, const char* expression, const char* file, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
	}
}


inline int Finish(const char* testName) {
	if (failureCount == 0) {
		std::printf("%s: all checks passed\n", testName);
		return 0;
	}

	std::printf("%s: %d check(s) failed\n", testName, failureCount);
	return 1;
}


// Keeps everything written to it, so a test can read the console output.
class RecordingPrint : public Print {
private:
	std::string written;


public:
	void WriteRaw(const char* data, size_t size) override {
		written.append(data, size);
	}


	const std::string& Text() const {
		return written;
	}


	void Clear() {
		written.clear();
	}
};


}


#define CommandExpect(condition) CommandSim::Expect((condition), #condition, __FILE__, __LINE__)
