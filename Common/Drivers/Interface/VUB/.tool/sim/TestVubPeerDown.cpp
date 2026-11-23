#include "SimCheck.h"
#include "VubChains.h"
#include "VubDoubles.h"
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// A peer that is down is not a link that is down. While the link is up and a layer has declared the peer down, the layers' own packets
// go out and the application's packets wait. While the link is down, nothing goes out.


// The packet a layer sends from its Process when testSendOwn is set: the byte testOwnValue.
inline bool testSendOwn = false;
inline uint8 testOwnValue = 0xC1;

// The peer call of the layer's next Process: 1 declares the peer up, -1 down, 0 makes no call.
inline int testPeerCall = 0;


// The packets a line delivered to one end, with their bytes.
struct PacketLog {
	std::array<std::array<uint8, 64>, 16> bytes{};
	std::array<std::size_t, 16> length{};
	std::size_t count = 0;


	void FeedPacket(std::span<const uint8> packet) {
		if (count < length.size()) {
			for (std::size_t index = 0; index < packet.size(); index++) {
				bytes[count][index] = packet[index];
			}

			length[count] = packet.size();
		}

		count++;
	}
};


// A layer with no header that declares its peer from its Process, and sends a one-byte packet of its own when asked.
struct OwnPeerLayer {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x4F574E31;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;


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


	void Process(uint32, const VubOutbox& outbox) {
		if (testPeerCall != 0) {
			outbox.SetPeer(testPeerCall > 0);
			testPeerCall = 0;
		}

		if (testSendOwn) {
			std::array<uint8, 1> own = {testOwnValue};
			(void)outbox.Send(std::span<const uint8>(own.data(), own.size()));
			testSendOwn = false;
		}
	}
};


// The same layer over a framed stream line: a frame is the payload with its checks, between flags (FlagFraming).
using FramedOwnStack = Vub<+[] {
	return VubBuilder()
		.Layer<OwnPeerLayer>()
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A chain whose application packets wait while the connection is down, over a line that reports its state.
using OwnStack = Vub<+[] {
	return VubBuilder()
		.Layer<OwnPeerLayer>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using OwnFailStack = Vub<+[] {
	return VubBuilder()
		.Layer<OwnPeerLayer>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.WhileDown(VubDown::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A layer that declares its peer down from its first Process, and keeps sending a ping (kind 0) until it hears a packet from the
// peer. Every packet it hears marks the peer up. Its data packets (kind 1) go up to the receive handler.
struct RecoverLayer {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x524C5931;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;
	static constexpr uint8 KindPing = 0;
	static constexpr uint8 KindData = 1;


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = KindData;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		if (packet.size() < header) {
			return ResultStatus::invalidData;
		}

		heard = true;
		return VubTake{header, packet[0] == KindData};
	}


	void Process(uint32, const VubOutbox& outbox) {
		if (!started) {
			started = true;
			outbox.SetPeer(false);
		}

		if (heard) {
			heard = false;
			peerUp = true;
			outbox.SetPeer(true);
		}

		if (!peerUp) {
			std::array<uint8, 1> ping = {KindPing};
			(void)outbox.Send(std::span<const uint8>(ping.data(), ping.size()));
		}
	}


	bool started = false;
	bool heard = false;
	bool peerUp = false;
};


using RecoverStack = Vub<+[] {
	return VubBuilder()
		.Layer<RecoverLayer>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// While the link is up and the peer is down, the layer's own packet goes out at once and the application's packet waits. When the peer is
// up again the application's packet goes.
void TestOwnPacketGoesWhilePeerDown() {
	ResetTestState();
	testSendOwn = false;
	testPeerCall = 0;
	ReportingPacketLine line(VubFaults{});
	OwnStack stack(line.a);
	PacketLog log;
	stack.Process();
	SIM_EXPECT(stack.LinkUp());

	testPeerCall = -1;
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	std::array<uint8, 2> payload = {0xA0, 0xA1};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	testSendOwn = true;
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 1);
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 1);
	line.DeliverToB(log);
	SIM_EXPECT(log.count == 1);
	SIM_EXPECT(log.length[0] == 1);
	SIM_EXPECT(log.bytes[0][0] == testOwnValue);

	testPeerCall = 1;
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(line.toB.Counts().pushed == 2);
	line.DeliverToB(log);
	SIM_EXPECT(log.count == 2);
	SIM_EXPECT(log.length[1] == 2);
	SIM_EXPECT(log.bytes[1][0] == 0xA0);
	SIM_EXPECT(log.bytes[1][1] == 0xA1);
}


// The same with WhileDown(Fail): the layer's own packet is accepted while the peer is down, and the application's Send is refused.
void TestOwnPacketGoesUnderFailWhilePeerDown() {
	ResetTestState();
	testSendOwn = false;
	testPeerCall = 0;
	ReportingPacketLine line(VubFaults{});
	OwnFailStack stack(line.a);
	PacketLog log;
	stack.Process();
	testPeerCall = -1;
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	std::array<uint8, 2> payload = {0xA0, 0xA1};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::noConnection);
	testSendOwn = true;
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 1);
	line.DeliverToB(log);
	SIM_EXPECT(log.count == 1);
	SIM_EXPECT(log.bytes[0][0] == testOwnValue);
	SIM_EXPECT(stack.Drops().linkDown == 0);
}


// With the link down nothing goes out, neither the layer's packet nor the application's, and the layer's packet goes when the link is up.
void TestNothingGoesWhileLinkDown() {
	ResetTestState();
	testSendOwn = false;
	testPeerCall = 0;
	ReportingPacketLine line(VubFaults{});
	OwnStack stack(line.a);
	PacketLog log;
	stack.Process();
	line.SetLinkUp(false);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());

	std::array<uint8, 2> payload = {0xA0, 0xA1};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	testSendOwn = true;
	stack.Process();
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 0);

	line.SetLinkUp(true);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(line.toB.Counts().pushed == 2);
	line.DeliverToB(log);
	SIM_EXPECT(log.count == 2);
	SIM_EXPECT(log.length[0] == 1);
	SIM_EXPECT(log.length[1] == 2);
}


// A frame that is partly written when the peer goes down. The application's frame waits: its rest is not sent while the peer is down, and
// the whole frame goes again from its first byte when the peer is up. The frame is 7E A0 A1 41 01 7E (sum 0x41, XOR 0x01).
void TestPartlyWrittenAppFrameWaitsWhilePeerDown() {
	ResetTestState();
	testSendOwn = false;
	testPeerCall = 0;
	ReportingStreamLine line(VubFaults{});
	FramedOwnStack stack(line.a);
	RecordSink sink;
	stack.Process();
	std::array<uint8, 2> payload = {0xA0, 0xA1};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	line.a.StartRound(2);
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 2);

	testPeerCall = -1;
	line.a.StartRound(10);
	stack.Process();
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(line.toB.Counts().pushed == 2);

	testPeerCall = 1;
	line.a.StartRound(10);
	stack.Process();
	SIM_EXPECT(stack.LinkUp());
	SIM_EXPECT(line.toB.Counts().pushed == 8);
	line.DeliverToB(sink);
	SIM_EXPECT(sink.byteCount == 8);
	SIM_EXPECT(sink.bytes[0] == 0x7E);
	SIM_EXPECT(sink.bytes[1] == 0xA0);
	SIM_EXPECT(sink.bytes[2] == 0x7E);
	SIM_EXPECT(sink.bytes[3] == 0xA0);
	SIM_EXPECT(sink.bytes[4] == 0xA1);
	SIM_EXPECT(sink.bytes[5] == 0x41);
	SIM_EXPECT(sink.bytes[6] == 0x01);
	SIM_EXPECT(sink.bytes[7] == 0x7E);
}


// A layer's own frame that is partly written when the peer goes down goes on while the peer is down: its packets still go out. The own
// frame of byte C1 is 7E C1 C1 C1 7E (sum and XOR are C1).
void TestPartlyWrittenOwnFrameGoesWhilePeerDown() {
	ResetTestState();
	testSendOwn = false;
	testPeerCall = 0;
	ReportingStreamLine line(VubFaults{});
	FramedOwnStack stack(line.a);
	RecordSink sink;
	stack.Process();
	line.a.StartRound(2);
	testSendOwn = true;
	stack.Process();
	SIM_EXPECT(line.toB.Counts().pushed == 2);

	testPeerCall = -1;
	line.a.StartRound(10);
	stack.Process();
	SIM_EXPECT(!stack.LinkUp());
	SIM_EXPECT(line.toB.Counts().pushed == 7);
	line.DeliverToB(sink);
	SIM_EXPECT(sink.byteCount == 7);
	SIM_EXPECT(sink.bytes[0] == 0x7E);
	SIM_EXPECT(sink.bytes[1] == testOwnValue);
	SIM_EXPECT(sink.bytes[2] == 0x7E);
	SIM_EXPECT(sink.bytes[3] == testOwnValue);
	SIM_EXPECT(sink.bytes[4] == testOwnValue);
	SIM_EXPECT(sink.bytes[5] == testOwnValue);
	SIM_EXPECT(sink.bytes[6] == 0x7E);
}


// Two stacks whose layers both declared the other down. Each layer's ping reaches the other end, and the other end is up. Then an
// application packet goes end to end.
void TestPeersRecoverWhenEachSendsItsOwn() {
	ResetTestState();
	PacketLine line(VubFaults{});
	RecoverStack a(line.a);
	RecoverStack b(line.b);
	a.Process();
	b.Process();
	SIM_EXPECT(!a.LinkUp());
	SIM_EXPECT(!b.LinkUp());
	line.DeliverToB(b);
	line.DeliverToA(a);

	a.Process();
	b.Process();
	SIM_EXPECT(a.LinkUp());
	SIM_EXPECT(b.LinkUp());
	SIM_EXPECT(testReceivedCount == 0);

	std::array<uint8, 2> payload = {0x31, 0x32};
	SIM_EXPECT(a.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	a.Process();
	line.DeliverToB(b);
	b.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x31);
}


int main() {
	TestOwnPacketGoesWhilePeerDown();
	TestOwnPacketGoesUnderFailWhilePeerDown();
	TestNothingGoesWhileLinkDown();
	TestPartlyWrittenAppFrameWaitsWhilePeerDown();
	TestPartlyWrittenOwnFrameGoesWhilePeerDown();
	TestPeersRecoverWhenEachSendsItsOwn();
	return SimFinish("TestVubPeerDown");
}
