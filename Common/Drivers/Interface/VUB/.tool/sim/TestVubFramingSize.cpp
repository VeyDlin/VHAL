#include "SimCheck.h"
#include "Chains.h"
#include <cstddef>


// The size of every chain of Chains.h is pinned at the value it had before the time-aware framings came in: the receive time of a
// byte is kept only for a chain whose framing uses time, so no other chain may grow. A change of one of these numbers is a change of
// the RAM of a chain, and it has to be made on purpose.
//
// The numbers follow the parts a chain builds: the receive buffer of a stream chain is the packet bound of its MaxPayload, and the send
// queue and the drop counters are built only where the chain asks for them (Chains.h gives its chains SendQueue(4) and CountDrops()).
// A chain with a layer also holds one flag for a layer's busy send (see the stack's layerHolds): the packet chains with a layer are 888
// bytes plus that flag, rounded to 896. The stream chains keep their size, since the flag fits in the padding after their storage.


void TestChainSizesUnchanged() {
	SIM_EXPECT(sizeof(StreamStack) == 1176);
	SIM_EXPECT(sizeof(BeaconStack) == 1176);
	SIM_EXPECT(sizeof(PacketStack) == 896);
	SIM_EXPECT(sizeof(BulkStack) == 896);
	SIM_EXPECT(sizeof(ReportingStreamStack) == 944);
	SIM_EXPECT(sizeof(SilentStreamStack) == 928);
	SIM_EXPECT(sizeof(ReportingLengthStack) == 752);
	SIM_EXPECT(sizeof(FailingLengthStack) == 752);
	SIM_EXPECT(sizeof(FailingStreamStack) == 944);
	SIM_EXPECT(sizeof(ReportingPacketStack) == 680);
	SIM_EXPECT(sizeof(FailingPacketStack) == 680);
	SIM_EXPECT(sizeof(ProbeStack) == 896);
}


int main() {
	TestChainSizesUnchanged();
	return SimFinish("TestVubFramingSize");
}
