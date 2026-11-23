#include "SimCheck.h"
#include "Chains.h"
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Gap.h>
#include <Drivers/Interface/VUB/Layer/KeepAlive.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <cstddef>
#include <type_traits>
#include <utility>


// The options of a built-in block are template arguments of the block the stack stores. The chain's options are the stored type's, so a
// stored Reliable with Retries(5) is ReliableOf with the options as its last template argument, and its options are constants.
constexpr VUB::ReliableOptions<32, 4> FiveRetries{.retries = 5};


using FiveStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Reliable>().Retries(5)
		.Link<PacketEnd>()
		.MaxPayload(32)
		.ReceiveQueue(3)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using DefaultStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Reliable>()
		.Link<PacketEnd>()
		.MaxPayload(32)
		.ReceiveQueue(3)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The stored Reliable is the layer with the chain's options; a layer with no option set is the default layer.
static_assert(std::is_same_v<std::remove_reference_t<decltype(std::declval<FiveStack&>().Find<VUB::Reliable>())>,
	VUB::ReliableOf<32, 4, false, FiveRetries>>);
static_assert(std::is_same_v<std::remove_reference_t<decltype(std::declval<DefaultStack&>().Find<VUB::Reliable>())>, VUB::Reliable>);
static_assert(FiveRetries.retries == 5);
static_assert(VUB::ReliableOf<32, 4, false, FiveRetries>::options.retries == 5);
static_assert(VUB::Reliable::options.retries == 3);
static_assert(std::is_same_v<VUB::Reliable, VUB::ReliableOf<32, 4, false, VUB::ReliableOptions<32, 4>{}>>);


// Options that are on take no RAM: the layer with Retries(5) is as large as the default layer.
static_assert(sizeof(VUB::ReliableOf<32, 4, false, FiveRetries>) == sizeof(VUB::Reliable));


// A gap block given its options in the chain is stored with them as template arguments; SetBaud still changes its baud rate.
using GapStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>().GapUs(1000).Baud(9600)
		.Link<StreamEnd>()
		.MaxPayload(32)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


constexpr VUB::GapOptions GapValue{.gapUs = 1000, .baud = 9600};
static_assert(std::is_same_v<std::remove_reference_t<decltype(std::declval<GapStack&>().Find<VUB::Gap>())>, VUB::GapOf<GapValue>>);


// A keep-alive layer given its times in the chain is stored with them as template arguments.
using KeepAliveStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(1000)
		.DownTime(3000)
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


constexpr VUB::KeepAliveOptions KeepAliveValue{.quietUs = 1000, .downUs = 3000};
static_assert(std::is_same_v<std::remove_reference_t<decltype(std::declval<KeepAliveStack&>().Find<VUB::KeepAlive>())>,
	VUB::KeepAliveOf<KeepAliveValue>>);


void TestConfiguredStackRuns() {
	PacketLine line(VUB::Faults{});
	FiveStack stack(line.a);
	stack.Find<VUB::Reliable>().SetSession(0x1234);
	SIM_EXPECT(stack.Find<VUB::Reliable>().options.retries == 5);
}


int main() {
	TestConfiguredStackRuns();
	return SimFinish("TestVubConfigured");
}
