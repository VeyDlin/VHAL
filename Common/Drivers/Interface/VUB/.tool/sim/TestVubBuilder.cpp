#include "SimCheck.h"
#include "Chains.h"
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <type_traits>


// The sizes of the stream chain, worked out by hand: payload 40, layer header 3, XOR growth 2, so the packet is 45 bytes. The flag
// framing adds up to 45 + 6 + 45 = 96 bytes. The link's MaxPacket is 64, and the receive buffer is the link's MaxPacket.
static_assert(StreamStack::count == 4);
static_assert(StreamStack::packetBytes == 45);
static_assert(StreamStack::framedBytes == 96);
static_assert(StreamStack::streamLink);
static_assert(StreamStack::receiveBytes == 64);

// The packet chain has no framing, so its framed size is its packet size.
static_assert(PacketStack::packetBytes == 45);
static_assert(PacketStack::framedBytes == 45);
static_assert(!PacketStack::streamLink);

// The options of the last block can be read and replaced by value, and the type of the options must match.
static_assert(VUB::Builder().Layer<TestLayer>(TestLayerOptions{5, false}).WithLastOptions(TestLayerOptions{7, true}).LastOptions().beaconIntervalUs == 7);
static_assert(VUB::Builder().Layer<TestLayer>(TestLayerOptions{5, false}).WithLastOptions(TestLayerOptions{7, true}).LastOptions().bulk);
static_assert(VUB::Builder().Layer<TestLayer>().LastIs<TestLayer>());
static_assert(!VUB::Builder().Layer<TestLayer>().LastIs<XorTransform>());
static_assert(VUB::Builder().Transform<XorTransform>().Link<StreamEnd>().LastIs<StreamEnd>());


// A chain with no stored block: its storage is an empty type, and the stack takes no room for it. Without [[no_unique_address]] the
// empty storage takes a byte, and the padding after it can add 8 bytes to the stack (on a 64-bit host, with a receive queue of 3, the
// stack is 592 bytes instead of 584).
using NoBlockStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(3)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


static_assert(std::is_empty_v<NoBlockStack::Storage>);


void TestNoBlockStorageTakesNoRoom() {
	SIM_EXPECT(sizeof(NoBlockStack) == 584);
}


void TestBuildRunsSend() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StreamStack stack(line.a);

	std::array<uint8, 41> tooLong{};
	SIM_EXPECT(stack.Send(std::span<const uint8>(tooLong.data(), tooLong.size())) == ResultStatus::messageTooLong);

	std::array<uint8, 40> payload{};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
}


// The builder keeps the options a block was given: the layer of the beacon chain is built with its options value.
void TestOptionsReachTheBlock() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	BeaconStack beacon(line.a);
	testNowUs = 0;
	beacon.Process();
	SIM_EXPECT(testBeaconsSent == 1);
}


int main() {
	TestBuildRunsSend();
	TestOptionsReachTheBlock();
	TestNoBlockStorageTakesNoRoom();
	return SimFinish("TestVubBuilder");
}
