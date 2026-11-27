#include <VhalCheck.h>


// Self-test of the failure path. Both checks fail, so the program must return 1: Finish returns 1 when a check failed. The test is
// registered with WILL_FAIL, so CTest reports it as passing when the exit code is not 0.


int main() {
	VHAL_CHECK(1 == 2);
	VHAL_CHECK_EQ(3, 4);

	return VhalCheck::Finish("TestFrameFailure");
}
