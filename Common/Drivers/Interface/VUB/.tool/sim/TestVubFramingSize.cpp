#include "SimCheck.h"
#include "Chains.h"
#include <cstddef>


// The size of every chain of Chains.h is pinned at the value it had before the time-aware framings came in: the receive time of a
// byte is kept only for a chain whose framing uses time, so no other chain may grow. A change of one of these numbers is a change of
// the RAM of a chain, and it has to be made on purpose.


void TestChainSizesUnchanged() {
	SIM_EXPECT(sizeof(StreamStack) == 1192);
	SIM_EXPECT(sizeof(BeaconStack) == 1192);
	SIM_EXPECT(sizeof(PacketStack) == 888);
	SIM_EXPECT(sizeof(BulkStack) == 888);
	SIM_EXPECT(sizeof(ReportingStreamStack) == 968);
	SIM_EXPECT(sizeof(SilentStreamStack) == 952);
	SIM_EXPECT(sizeof(ReportingLengthStack) == 776);
	SIM_EXPECT(sizeof(FailingLengthStack) == 776);
	SIM_EXPECT(sizeof(FailingStreamStack) == 968);
	SIM_EXPECT(sizeof(ReportingPacketStack) == 680);
	SIM_EXPECT(sizeof(FailingPacketStack) == 680);
	SIM_EXPECT(sizeof(ProbeStack) == 888);
}


int main() {
	TestChainSizesUnchanged();
	return SimFinish("TestVubFramingSize");
}
