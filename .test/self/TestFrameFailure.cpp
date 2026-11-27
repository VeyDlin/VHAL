#include <VhalCheck.h>


// Self-test of the failure path. Both checks fail on purpose and print their failure lines. Finish must then report two of two
// checks failed and return 1. The program itself passes when that holds, so a crash or an abort fails it.


int main() {
	VHAL_CHECK(1 == 2);
	VHAL_CHECK_EQ(3, 4);

	int result = VhalCheck::Finish("TestFrameFailure");
	bool holds = result == 1 && VhalCheck::failCount == 2 && VhalCheck::checkCount == 2;

	std::printf("TestFrameFailure: the failure path %s\n", holds ? "holds" : "does not hold");
	return holds ? 0 : 1;
}
