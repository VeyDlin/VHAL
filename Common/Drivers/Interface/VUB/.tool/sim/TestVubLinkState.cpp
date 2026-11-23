#include "SimCheck.h"
#include "VubChains.h"
#include "VubDoubles.h"
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubBuilder.h>
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The link state of a stack, and the layers it tells. The stack is built on a line; the line's SetLinkUp takes it down and up.


// The events a WatchLayer was told: the state and the time of each OnLink, in order.
inline std::array<bool, 8> testLinkUp{};
inline std::array<uint32, 8> testLinkNow{};
inline std::size_t testLinkEvents = 0;

// The peer call a PeerLayer makes from its next Process: 1 declares the peer up, -1 down, and 0 makes no call.
inline int testPeerCall = 0;

// The framings' Reset calls, counted by CountingFraming.
inline std::size_t testFramingResets = 0;


// A layer that records each OnLink it is told.
struct WatchLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x57415431;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8>) {
		return VubTake{0, true};
	}


	void Process(uint32, const VubOutbox&) {
	}


	void OnLink(bool up, uint32 now) {
		if (testLinkEvents < testLinkUp.size()) {
			testLinkUp[testLinkEvents] = up;
			testLinkNow[testLinkEvents] = now;
		}

		testLinkEvents++;
	}
};


// A layer that declares its peer's state from Process, through its outbox.
struct PeerLayer : WatchLayer {
	static constexpr bool reportsPeer = true;


	void Process(uint32, const VubOutbox& outbox) {
		if (testPeerCall != 0) {
			outbox.SetPeer(testPeerCall > 0);
			testPeerCall = 0;
		}
	}
};


// Flag framing that counts its resets.
struct CountingFraming : FlagFraming {
	void Reset() {
		testFramingResets++;
		FlagFraming::Reset();
	}
};


using WatchStack = Vub<+[] {
	return VubBuilder()
		.Layer<WatchLayer>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A layer that declares the peer, over a link that does not report: the layer alone takes the stack down.
using PeerStack = Vub<+[] {
	return VubBuilder()
		.Layer<PeerLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using CountingStack = Vub<+[] {
	return VubBuilder()
		.Framing<CountingFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The trait is off unless a link says it reports, and the builder sees it at compile time.
static_assert(!StreamEnd::traits.reportsState);
static_assert(!PacketEnd::traits.reportsState);
static_assert(ReportingStreamEnd::traits.reportsState);
static_assert(ReportingPacketEnd::traits.reportsState);
static_assert(VubLinkTraits().ReportsState().reportsState);
static_assert(!VubLinkTraits().MaxPacket(8).reportsState);

// A link with the trait has the SetStateSink member the stack calls. A link without the trait does not need it.
static_assert(VubReportingLink<ReportingStreamEnd>);
static_assert(VubReportingLink<ReportingPacketEnd>);
static_assert(!VubReportingLink<StreamEnd>);
static_assert(!VubReportingLink<PacketEnd>);

// The state API exists only on a stack with something to report: a link with the trait, or a layer that reports its peer.
template<typename StackType>
concept HasLinkUp = requires(const StackType& stack) {
	stack.LinkUp();
};


template<typename StackType>
concept HasLinkDowns = requires(const StackType& stack) {
	stack.LinkDowns();
};


static_assert(!HasLinkUp<StreamStack>);
static_assert(!HasLinkUp<PacketStack>);
static_assert(!HasLinkDowns<PacketStack>);
static_assert(HasLinkUp<ReportingStreamStack>);
static_assert(HasLinkDowns<ReportingPacketStack>);
static_assert(HasLinkUp<PeerStack>);
static_assert(HasLinkUp<WatchStack>);

// A chain that cannot report keeps its size: the stack's storage for the link state is empty. The sizes are of the host's 64-bit build.
static_assert(sizeof(StreamStack) == 1192);
static_assert(sizeof(PacketStack) == 888);

// The option is a value of the builder: Wait by default, Fail when given.
static_assert(VubBuilder().settings.whileDown == VubDown::Wait);
static_assert(VubBuilder().WhileDown(VubDown::Fail).settings.whileDown == VubDown::Fail);


// The layer is told of each change, in order, with the time of the Process that saw it. A Process with no change tells it nothing. The
// first report of up, when the stack is built, is the first change.
void TestLayerIsTold() {
	ResetTestState();
	testLinkEvents = 0;
	ReportingPacketLine line(VubFaults{});
	WatchStack stack(line.a);
	testNowUs = 100;
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(testLinkEvents == 1);
	SIM_EXPECT(testLinkUp[0]);
	SIM_EXPECT(testLinkNow[0] == 100);

	testNowUs = 200;
	stack.Process();
	SIM_EXPECT(testLinkEvents == 1);

	line.SetLinkUp(false);
	testNowUs = 300;
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(testLinkEvents == 2);
	SIM_EXPECT(!testLinkUp[1]);
	SIM_EXPECT(testLinkNow[1] == 300);

	line.SetLinkUp(true);
	testNowUs = 400;
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(testLinkEvents == 3);
	SIM_EXPECT(testLinkUp[2]);
	SIM_EXPECT(testLinkNow[2] == 400);
}


// A framing with Reset is reset on each change of the connection, in both directions, and not on a Process with no change.
void TestFramingResetOnChange() {
	ResetTestState();
	testFramingResets = 0;
	ReportingStreamLine line(VubFaults{});
	CountingStack stack(line.a);
	stack.Process();
	SIM_EXPECT(testFramingResets == 1);
	stack.Process();
	SIM_EXPECT(testFramingResets == 1);
	line.SetLinkUp(false);
	stack.Process();
	SIM_EXPECT(testFramingResets == 2);
	line.SetLinkUp(true);
	stack.Process();
	SIM_EXPECT(testFramingResets == 3);
}


// A link that goes down and up between two Process calls is still seen: the layer is told down, then up, and the down is counted.
void TestDownAndUpBetweenProcessIsSeen() {
	ResetTestState();
	testLinkEvents = 0;
	ReportingPacketLine line(VubFaults{});
	WatchStack stack(line.a);
	stack.Process();
	line.SetLinkUp(false);
	line.SetLinkUp(true);
	testNowUs = 50;
	stack.Process();
	SIM_EXPECT(testLinkEvents == 3);
	SIM_EXPECT(!testLinkUp[1]);
	SIM_EXPECT(testLinkUp[2]);
	SIM_EXPECT(testLinkNow[1] == 50);
	SIM_EXPECT(testLinkNow[2] == 50);
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(stack.LinkDowns() == 1);
}


// Each fall from up counts once. A second report of down while down is not another fall.
void TestDownsCountFallsFromUp() {
	ResetTestState();
	ReportingPacketLine line(VubFaults{});
	ReportingPacketStack stack(line.a);
	stack.Process();
	line.SetLinkUp(false);
	stack.Process();
	SIM_EXPECT(stack.LinkDowns() == 1);
	line.SetLinkUp(false);
	stack.Process();
	SIM_EXPECT(stack.LinkDowns() == 1);
	line.SetLinkUp(true);
	stack.Process();
	line.SetLinkUp(false);
	stack.Process();
	SIM_EXPECT(stack.LinkDowns() == 2);
}


// A layer declares its peer down from Process: the stack is down, the layer is told, and nothing is offered to the link while it is
// down. Declaring the peer up again brings the stack up, and the queued packet goes.
void TestPeerDeclaredFromProcess() {
	ResetTestState();
	testLinkEvents = 0;
	testPeerCall = 0;
	PacketLine line(VubFaults{});
	PeerStack stack(line.a);
	std::array<uint8, 2> payload = {0x01, 0x02};
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 1);

	testPeerCall = -1;
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(testLinkEvents == 1);
	SIM_EXPECT(!testLinkUp[0]);
	SIM_EXPECT(stack.LinkDowns() == 1);

	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 1);

	testPeerCall = 1;
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(testLinkEvents == 2);
	SIM_EXPECT(testLinkUp[1]);
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 2);
}


// A stream chain with a peer layer and a counting framing, over a link that reports: a down resets the framing and drops the receive ring.
using PeerFramedStack = Vub<+[] {
	return VubBuilder()
		.Layer<PeerLayer>()
		.Framing<CountingFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A fall of the link while the peer is already down is no second down: the stack stays down, the framing is not reset again, and the
// receive ring is kept.
void TestFallWhilePeerDownChangesNothing() {
	ResetTestState();
	testLinkEvents = 0;
	testPeerCall = 0;
	testFramingResets = 0;
	ReportingStreamLine line(VubFaults{});
	PeerFramedStack stack(line.a);
	stack.Process();
	testPeerCall = -1;
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(stack.LinkDowns() == 1);
	std::size_t resetsBefore = testFramingResets;
	stack.Feed(0x04);
	line.SetLinkUp(false);
	line.SetLinkUp(true);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(stack.LinkDowns() == 1);
	SIM_EXPECT(testFramingResets == resetsBefore);
	SIM_EXPECT(stack.Drops().linkDown == 0);
}


int main() {
	TestLayerIsTold();
	TestFramingResetOnChange();
	TestDownAndUpBetweenProcessIsSeen();
	TestDownsCountFallsFromUp();
	TestPeerDeclaredFromProcess();
	TestFallWhilePeerDownChangesNothing();
	return SimFinish("TestVubLinkState");
}
