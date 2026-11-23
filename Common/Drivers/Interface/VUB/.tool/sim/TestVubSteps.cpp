#include "SimCheck.h"
#include "VubChains.h"
#include "VubStepDoubles.h"
#include <Drivers/Interface/VUB/VubBuilder.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <type_traits>


// A block with a Step is recognised, and a block without one is not.
static_assert(VubHasStep<StepLayer>);
static_assert(!VubHasStep<TestLayer>);

// The selector gives the Step of a block with one, and the next builder of a block without one.
static_assert(!std::is_same_v<VubStepOf<StepLayer, VubBuilder<>>::type, VubBuilder<>>);
static_assert(std::is_same_v<VubStepOf<TestLayer, VubBuilder<>>::type, VubBuilder<>>);

// The option methods set the fields of the last block's options, and keep the block.
static_assert(VubBuilder().Layer<StepLayer>().First(5).LastOptions().first == 5);
static_assert(VubBuilder().Layer<StepLayer>().First(5).LastOptions().second == 2);
static_assert(VubBuilder().Layer<StepLayer>().First(5).Second(9).LastOptions().first == 5);
static_assert(VubBuilder().Layer<StepLayer>().First(5).Second(9).LastOptions().second == 9);
static_assert(VubBuilder().Layer<StepLayer>().First(5).Second(9).LastIs<StepLayer>());

// Options given as a value are kept, and the option methods change one of them.
static_assert(VubBuilder().Layer<StepLayer>(StepLayer::Options{3, 4}).Second(9).LastOptions().first == 3);
static_assert(VubBuilder().Layer<StepLayer>(StepLayer::Options{3, 4}).Second(9).LastOptions().second == 9);

// The Step keeps every builder method: the chain goes on after the option methods.
static_assert(VubBuilder().Layer<StepLayer>().First(5).Second(9).Layer<TestLayer>().LastIs<TestLayer>());
static_assert(VubBuilder().Layer<StepLayer>().First(5).Second(9).Layer<TestLayer>().count == 2);


// The stack's layer gets the options from the chain: First 5 and Second 9.
using StepStack = Vub<+[] {
	return VubBuilder()
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
using DefaultStepStack = Vub<+[] {
	return VubBuilder()
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
using ValueStepStack = Vub<+[] {
	return VubBuilder()
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
	PacketLine line(VubFaults{});
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
	PacketLine line(VubFaults{});
	DefaultStepStack stack(line.a);
	SIM_EXPECT(testStepFirst == 1);
	SIM_EXPECT(testStepSecond == 2);
}


void TestValueOptionsWithOptionMethod() {
	ResetTestState();
	testStepFirst = 0;
	testStepSecond = 0;
	PacketLine line(VubFaults{});
	ValueStepStack stack(line.a);
	SIM_EXPECT(testStepFirst == 3);
	SIM_EXPECT(testStepSecond == 9);
}


int main() {
	TestOptionMethodsReachTheLayer();
	TestChainWithoutOptionCallsGetsDefaults();
	TestValueOptionsWithOptionMethod();
	return SimFinish("TestVubSteps");
}
