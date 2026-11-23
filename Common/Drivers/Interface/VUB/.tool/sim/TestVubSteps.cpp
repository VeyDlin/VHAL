#include "SimCheck.h"
#include "Chains.h"
#include "StepDoubles.h"
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <type_traits>


// A block with a Step is recognised, and a block without one is not.
static_assert(VUB::BlockHasStep<StepLayer>);
static_assert(!VUB::BlockHasStep<TestLayer>);

// The selector gives the Step of a block with one, and the next builder of a block without one.
static_assert(!std::is_same_v<VUB::StepOf<StepLayer, VUB::Builder<>>::type, VUB::Builder<>>);
static_assert(std::is_same_v<VUB::StepOf<TestLayer, VUB::Builder<>>::type, VUB::Builder<>>);

// The option methods set the fields of the last block's options, and keep the block.
static_assert(VUB::Builder().Layer<StepLayer>().First(5).LastOptions().first == 5);
static_assert(VUB::Builder().Layer<StepLayer>().First(5).LastOptions().second == 2);
static_assert(VUB::Builder().Layer<StepLayer>().First(5).Second(9).LastOptions().first == 5);
static_assert(VUB::Builder().Layer<StepLayer>().First(5).Second(9).LastOptions().second == 9);
static_assert(VUB::Builder().Layer<StepLayer>().First(5).Second(9).LastIs<StepLayer>());

// Options given as a value are kept, and the option methods change one of them.
static_assert(VUB::Builder().Layer<StepLayer>(StepLayer::Options{3, 4}).Second(9).LastOptions().first == 3);
static_assert(VUB::Builder().Layer<StepLayer>(StepLayer::Options{3, 4}).Second(9).LastOptions().second == 9);

// The Step keeps every builder method: the chain goes on after the option methods.
static_assert(VUB::Builder().Layer<StepLayer>().First(5).Second(9).Layer<TestLayer>().LastIs<TestLayer>());
static_assert(VUB::Builder().Layer<StepLayer>().First(5).Second(9).Layer<TestLayer>().count == 2);


// The stack's layer gets the options from the chain: First 5 and Second 9.
using StepStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StepLayer>().First(5).Second(9)
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same chain without the option calls: the layer gets the defaults.
using DefaultStepStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StepLayer>()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Options given as a value, with an option method after them.
using ValueStepStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<StepLayer>(StepLayer::Options{3, 4}).Second(9)
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


void TestOptionMethodsReachTheLayer() {
	ResetTestState();
	testStepFirst = 0;
	testStepSecond = 0;
	PacketLine line(VUB::Faults{});
	StepStack stack(line.a);
	SIM_EXPECT(testStepFirst == 5);
	SIM_EXPECT(testStepSecond == 9);

	std::array<uint8, 40> payload{};
	SIM_EXPECT(stack.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
}


void TestChainWithoutOptionCallsGetsDefaults() {
	ResetTestState();
	testStepFirst = 0;
	testStepSecond = 0;
	PacketLine line(VUB::Faults{});
	DefaultStepStack stack(line.a);
	SIM_EXPECT(testStepFirst == 1);
	SIM_EXPECT(testStepSecond == 2);
}


void TestValueOptionsWithOptionMethod() {
	ResetTestState();
	testStepFirst = 0;
	testStepSecond = 0;
	PacketLine line(VUB::Faults{});
	ValueStepStack stack(line.a);
	SIM_EXPECT(testStepFirst == 3);
	SIM_EXPECT(testStepSecond == 9);
}


// A framing with a Step is recognised, and its option methods follow it in the chain.
static_assert(VUB::BlockHasStep<StepFraming>);

// Framing(options) keeps the value; the option method after it changes the field and keeps the block.
static_assert(VUB::Builder().Framing<StepFraming>().LastOptions().delimiter == 0x7E);
static_assert(VUB::Builder().Framing<StepFraming>().Delimiter(0x21).LastOptions().delimiter == 0x21);
static_assert(VUB::Builder().Framing<StepFraming>().Delimiter(0x21).LastIs<StepFraming>());
static_assert(VUB::Builder().Framing<StepFraming>(StepFraming::Options{0x33}).LastOptions().delimiter == 0x33);
static_assert(VUB::Builder().Framing<StepFraming>(StepFraming::Options{0x33}).Delimiter(0x21).LastOptions().delimiter == 0x21);
static_assert(VUB::Builder().Framing<StepFraming>(StepFraming::Options{0x33}).Delimiter(0x21).LastIs<StepFraming>());

// The chain goes on after the option methods of the framing: the link is added, and the block count is right.
static_assert(VUB::Builder().Framing<StepFraming>().Delimiter(0x21).Link<StreamEnd>().count == 2);
static_assert(VUB::Builder().Framing<StepFraming>(StepFraming::Options{0x33}).Link<StreamEnd>().count == 2);


// Four stacks over a byte stream: the framing gets its delimiter from the default, from Framing(options), from a Step after the default
// options, and from a Step after Framing(options). The Step's value wins over the options value.
using DefaultFramingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<StepFraming>()
		.Link<StreamEnd>()
		.MaxPayload(8)
		.ReceiveRing(64)
		.OnReceive<&TestReceive>();
}>;


using StepFramingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<StepFraming>().Delimiter(0x21)
		.Link<StreamEnd>()
		.MaxPayload(8)
		.ReceiveRing(64)
		.OnReceive<&TestReceive>();
}>;


using ValueFramingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<StepFraming>(StepFraming::Options{0x33})
		.Link<StreamEnd>()
		.MaxPayload(8)
		.ReceiveRing(64)
		.OnReceive<&TestReceive>();
}>;


using ValueStepFramingStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<StepFraming>(StepFraming::Options{0x33}).Delimiter(0x21)
		.Link<StreamEnd>()
		.MaxPayload(8)
		.ReceiveRing(64)
		.OnReceive<&TestReceive>();
}>;


// Sends the payload {01, 02} through the stack, and checks the wire: the framing frames it with the delimiter on both ends, and the
// constructor of the framing was given that delimiter.
template<typename Stack>
void CheckWireFramedBy(uint8 delimiter) {
	ResetTestState();
	testStepDelimiter = 0;
	StreamLine line(VUB::Faults{});
	Stack sender(line.a);
	SIM_EXPECT(testStepDelimiter == delimiter);
	std::array<uint8, 2> payload = {0x01, 0x02};
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();
	RecordSink sink;
	line.DeliverToB(sink);
	SIM_EXPECT(sink.byteCount == 4);
	SIM_EXPECT(sink.bytes[0] == delimiter);
	SIM_EXPECT(sink.bytes[1] == 0x01);
	SIM_EXPECT(sink.bytes[2] == 0x02);
	SIM_EXPECT(sink.bytes[3] == delimiter);
}


void TestStepOnFramingReachesFraming() {
	CheckWireFramedBy<StepFramingStack>(0x21);
}


void TestFramingOptionsReachFraming() {
	CheckWireFramedBy<ValueFramingStack>(0x33);
}


void TestStepAfterFramingOptionsWins() {
	CheckWireFramedBy<ValueStepFramingStack>(0x21);
}


void TestDefaultFramingGetsDefault() {
	CheckWireFramedBy<DefaultFramingStack>(0x7E);
}


// A stack with the Step's framing sends a packet that the receiver with the same framing takes whole.
void TestStepFramingRoundTrip() {
	ResetTestState();
	StreamLine line(VUB::Faults{});
	StepFramingStack sender(line.a);
	StepFramingStack receiver(line.b);
	std::array<uint8, 2> payload = {0x01, 0x02};
	SIM_EXPECT(sender.Send(std::span<const uint8>(payload.data(), payload.size())) == ResultStatus::ok);
	sender.Process();
	line.DeliverToB(receiver);
	receiver.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x01);
	SIM_EXPECT(testReceived[0][1] == 0x02);
}


int main() {
	TestOptionMethodsReachTheLayer();
	TestChainWithoutOptionCallsGetsDefaults();
	TestValueOptionsWithOptionMethod();
	TestStepOnFramingReachesFraming();
	TestFramingOptionsReachFraming();
	TestStepAfterFramingOptionsWins();
	TestDefaultFramingGetsDefault();
	TestStepFramingRoundTrip();
	return SimFinish("TestVubSteps");
}
