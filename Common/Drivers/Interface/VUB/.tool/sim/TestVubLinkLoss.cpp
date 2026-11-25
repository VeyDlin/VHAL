#include "SimCheck.h"
#include "LinkFaults.h"
#include "LinkKit.h"
#include "UartDoubles.h"
#include "UsbDoubles.h"
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/BulkLink.h>
#include <Drivers/Interface/VUB/Link/CdcLink.h>
#include <Drivers/Interface/VUB/Link/HidLink.h>
#include <Drivers/Interface/VUB/Link/UartLink.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Arc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// VUB::Reliable over each link with loss in the link doubles. Every chain is Reliable, a CRC-16 and, on a byte stream, COBS: the CRC
// is what rejects a flipped bit, and COBS and the CRC together reject a dropped byte. Each end sends eight packets. The checks: every
// packet is ok, each arrives exactly once, the schedule damaged what it was given, and the stack counts the frames it rejects. A
// dropped HID report leaves no trace in the stack, so the double's own count is the check for it.


// Counts the outcomes the delivery layer reports, from both ends.
inline uint32 lossOk = 0;
inline uint32 lossFailed = 0;
inline uint16 lossMaxAttempts = 0;


// The outcomes of each payload, by its first byte: the ids of the run are below 16 and from 100 up.
inline std::array<uint8, 256> lossOutcomesOf{};


void LossOutcome(const VUB::Outcome& outcome) {
	if (outcome.status == ResultStatus::ok) {
		lossOk++;
	} else {
		lossFailed++;
	}

	if (outcome.attempts > lossMaxAttempts) {
		lossMaxAttempts = outcome.attempts;
	}

	if (!outcome.payload.empty()) {
		lossOutcomesOf[outcome.payload[0]]++;
	}
}


// What a run did: the items each direction lost or flipped, and the frames each end rejected.
struct LossCounts {
	uint32 droppedFromA = 0;
	uint32 droppedFromB = 0;
	uint32 flippedFromA = 0;
	uint32 flippedFromB = 0;
	std::size_t rejectedAtA = 0;
	std::size_t rejectedAtB = 0;
};


// The items 0 and 5 of each direction are always damaged; the seed adds one item in a hundred.
constexpr uint64 listedItems = (1ull << 0) | (1ull << 5);


constexpr LinkFaultPlan DropPlan(uint32 seed) {
	return LinkFaultPlan{seed, 1, 0, listedItems, 0};
}


constexpr LinkFaultPlan FlipPlan(uint32 seed) {
	return LinkFaultPlan{seed, 0, 1, 0, listedItems};
}


using LossUartLink = VUB::UartLink<FakeUart>;
using LossCdcLink = VUB::CdcLink<FakeCdc<32>>;
using LossBulkLink = VUB::BulkLink<FakeBulk<32>>;
using LossHidLink = VUB::HidLink<FakeHid<64, 2>, 64>;


using LossUartStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossUartLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using LossUartStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossUartLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


using LossCdcStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossCdcLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using LossCdcStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossCdcLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


using LossBulkStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossBulkLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using LossBulkStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<LossBulkLink>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


using LossHidStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<LossHidLink>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using LossHidStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(16).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&LossOutcome>()
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Link<LossHidLink>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


// Clears the run's counters and the link clock.
void ResetLossRun() {
	ResetLinkKit();
	lossOk = 0;
	lossFailed = 0;
	lossMaxAttempts = 0;
	lossOutcomesOf.fill(0);
}


// Sends one payload, and runs the round until the stack takes it. A stack with its window full is busy until an outcome, and an
// outcome of a lost packet comes after the last retry: 17 attempts of 200 ms, so the wait is under 8000 rounds of 1 ms.
template<typename Stack, typename Round>
void SendUntilOk(Stack& stack, const std::array<uint8, 24>& payload, Round round) {
	int attempts = 0;
	while (stack.Send(payload) != ResultStatus::ok && attempts < 8000) {
		round();
		attempts++;
	}

	SIM_EXPECT(attempts < 8000);
}


// Sends eight packets from each end, then runs 8000 rounds so every lost packet is resent and settled.
template<typename StackA, typename StackB, typename Round>
void RunLossTraffic(StackA& stackA, StackB& stackB, Round round) {
	for (uint8 id = 0; id < 8; id++) {
		std::array<uint8, 24> fromA = LinkPayload(id);
		std::array<uint8, 24> fromB = LinkPayload(static_cast<uint8>(id + 100));
		SendUntilOk(stackA, fromA, round);
		SendUntilOk(stackB, fromB, round);
	}

	for (int index = 0; index < 8000; index++) {
		round();
	}
}


// How many entries of the log are exactly the expected payload.
std::size_t OccurrencesIn(const PayloadLog& log, std::span<const uint8> expected) {
	std::size_t found = 0;
	for (std::size_t index = 0; index < log.count && index < log.packets.size(); index++) {
		if (log.Is(index, expected)) {
			found++;
		}
	}

	return found;
}


// Every packet of both ends: one outcome ok each, no failure, each delivered once at the other end, and a resend somewhere.
void CheckLossTraffic() {
	SIM_EXPECT(logAtB.count == 8);
	SIM_EXPECT(logAtA.count == 8);
	SIM_EXPECT(lossOk == 16);
	SIM_EXPECT(lossFailed == 0);
	SIM_EXPECT(lossMaxAttempts >= 2);
	for (uint8 id = 0; id < 8; id++) {
		std::array<uint8, 24> fromA = LinkPayload(id);
		std::array<uint8, 24> fromB = LinkPayload(static_cast<uint8>(id + 100));
		SIM_EXPECT(OccurrencesIn(logAtB, fromA) == 1);
		SIM_EXPECT(OccurrencesIn(logAtA, fromB) == 1);
	}
}


// The rejected frames of a stack: the framing rejects and the CRC rejects.
template<typename Stack>
std::size_t RejectedBy(const Stack& stack) {
	VUB::DropCounters counters = stack.Drops();
	return static_cast<std::size_t>(counters.framingError) + static_cast<std::size_t>(counters.transformError);
}


// The UART port, with the G4 behaviour (a transfer ends on FinishTransmit) or the ESP32 behaviour (it ends inside Write).
LossCounts RunUart(const LinkFaultPlan& planA, const LinkFaultPlan& planB, bool completesInWrite) {
	ResetLossRun();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	portA.completesInWrite = completesInWrite;
	portB.completesInWrite = completesInWrite;
	portA.faults = LinkFaults(planA);
	portB.faults = LinkFaults(planB);
	LossUartLink linkA(portA);
	LossUartLink linkB(portB);
	LossUartStackA stackA(linkA);
	LossUartStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	RunLossTraffic(stackA, stackB, [&]() {
		stackA.Process();
		portA.FinishTransmit();
		stackB.Process();
		portB.FinishTransmit();
		linkNowUs += 1000;
	});
	CheckLossTraffic();

	LossCounts counts;
	counts.droppedFromA = portA.faults.Dropped();
	counts.droppedFromB = portB.faults.Dropped();
	counts.flippedFromA = portA.faults.Flipped();
	counts.flippedFromB = portB.faults.Flipped();
	counts.rejectedAtA = RejectedBy(stackA);
	counts.rejectedAtB = RejectedBy(stackB);
	return counts;
}


// The CDC function pair: two queues, one for each direction, the links poll them.
LossCounts RunCdc(const LinkFaultPlan& planA, const LinkFaultPlan& planB) {
	ResetLossRun();
	ByteQueue<32> ab;
	ByteQueue<32> ba;
	FakeCdc<32> functionA(ab, ba);
	FakeCdc<32> functionB(ba, ab);
	functionA.SetConfigured(true);
	functionA.SetTerminalOpen(true);
	functionB.SetConfigured(true);
	functionB.SetTerminalOpen(true);
	functionA.faults = LinkFaults(planA);
	functionB.faults = LinkFaults(planB);
	LossCdcLink linkA(functionA);
	LossCdcLink linkB(functionB);
	LossCdcStackA stackA(linkA);
	LossCdcStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	RunLossTraffic(stackA, stackB, [&]() {
		stackA.Process();
		stackB.Process();
		linkNowUs += 1000;
	});
	CheckLossTraffic();

	LossCounts counts;
	counts.droppedFromA = functionA.faults.Dropped();
	counts.droppedFromB = functionB.faults.Dropped();
	counts.flippedFromA = functionA.faults.Flipped();
	counts.flippedFromB = functionB.faults.Flipped();
	counts.rejectedAtA = RejectedBy(stackA);
	counts.rejectedAtB = RejectedBy(stackB);
	return counts;
}


// The vendor bulk function pair: the same queues as the CDC pair, without DTR.
LossCounts RunBulk(const LinkFaultPlan& planA, const LinkFaultPlan& planB) {
	ResetLossRun();
	ByteQueue<32> ab;
	ByteQueue<32> ba;
	FakeBulk<32> functionA(ab, ba);
	FakeBulk<32> functionB(ba, ab);
	functionA.SetConfigured(true);
	functionB.SetConfigured(true);
	functionA.faults = LinkFaults(planA);
	functionB.faults = LinkFaults(planB);
	LossBulkLink linkA(functionA);
	LossBulkLink linkB(functionB);
	LossBulkStackA stackA(linkA);
	LossBulkStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	RunLossTraffic(stackA, stackB, [&]() {
		stackA.Process();
		stackB.Process();
		linkNowUs += 1000;
	});
	CheckLossTraffic();

	LossCounts counts;
	counts.droppedFromA = functionA.faults.Dropped();
	counts.droppedFromB = functionB.faults.Dropped();
	counts.flippedFromA = functionA.faults.Flipped();
	counts.flippedFromB = functionB.faults.Flipped();
	counts.rejectedAtA = RejectedBy(stackA);
	counts.rejectedAtB = RejectedBy(stackB);
	return counts;
}


// The HID pair: each input report of one function reaches the other's output through the sender's loss schedule. A dropped report
// leaves no trace in the stack, so only the double's count shows it; a flipped report is rejected by the CRC when the bit is in the packet.
LossCounts RunHid(const LinkFaultPlan& planA, const LinkFaultPlan& planB) {
	ResetLossRun();
	FakeHid<64, 2> hidA;
	FakeHid<64, 2> hidB;
	LossHidLink linkA(hidA);
	LossHidLink linkB(hidB);
	LossHidStackA stackA(linkA);
	LossHidStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	hidA.SetConfigured(true);
	hidB.SetConfigured(true);
	hidA.faults = LinkFaults(planA);
	hidB.faults = LinkFaults(planB);

	RunLossTraffic(stackA, stackB, [&]() {
		stackA.Process();
		hidA.DeliverInputsTo(hidB);
		stackB.Process();
		hidB.DeliverInputsTo(hidA);
		linkNowUs += 1000;
	});
	CheckLossTraffic();

	LossCounts counts;
	counts.droppedFromA = hidA.faults.Dropped();
	counts.droppedFromB = hidB.faults.Dropped();
	counts.flippedFromA = hidA.faults.Flipped();
	counts.flippedFromB = hidB.faults.Flipped();
	counts.rejectedAtA = RejectedBy(stackA);
	counts.rejectedAtB = RejectedBy(stackB);
	return counts;
}


void TestUartG4DropsAreRecovered() {
	LossCounts counts = RunUart(DropPlan(11), DropPlan(12), false);
	SIM_EXPECT(counts.droppedFromA >= 1);
	SIM_EXPECT(counts.droppedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestUartG4FlippedBitsAreRecovered() {
	LossCounts counts = RunUart(FlipPlan(13), FlipPlan(14), false);
	SIM_EXPECT(counts.flippedFromA >= 1);
	SIM_EXPECT(counts.flippedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestUartEspDropsAreRecovered() {
	LossCounts counts = RunUart(DropPlan(15), DropPlan(16), true);
	SIM_EXPECT(counts.droppedFromA >= 1);
	SIM_EXPECT(counts.droppedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestCdcDropsAreRecovered() {
	LossCounts counts = RunCdc(DropPlan(21), DropPlan(22));
	SIM_EXPECT(counts.droppedFromA >= 1);
	SIM_EXPECT(counts.droppedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestCdcFlippedBitsAreRecovered() {
	LossCounts counts = RunCdc(FlipPlan(23), FlipPlan(24));
	SIM_EXPECT(counts.flippedFromA >= 1);
	SIM_EXPECT(counts.flippedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestBulkDropsAreRecovered() {
	LossCounts counts = RunBulk(DropPlan(31), DropPlan(32));
	SIM_EXPECT(counts.droppedFromA >= 1);
	SIM_EXPECT(counts.droppedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestBulkFlippedBitsAreRecovered() {
	LossCounts counts = RunBulk(FlipPlan(33), FlipPlan(34));
	SIM_EXPECT(counts.flippedFromA >= 1);
	SIM_EXPECT(counts.flippedFromB >= 1);
	SIM_EXPECT(counts.rejectedAtA >= 1);
	SIM_EXPECT(counts.rejectedAtB >= 1);
}


void TestHidDropsAreRecovered() {
	LossCounts counts = RunHid(DropPlan(41), DropPlan(42));
	SIM_EXPECT(counts.droppedFromA >= 1);
	SIM_EXPECT(counts.droppedFromB >= 1);
}


void TestHidFlippedBitsAreRecovered() {
	LossCounts counts = RunHid(FlipPlan(43), FlipPlan(44));
	SIM_EXPECT(counts.flippedFromA >= 1);
	SIM_EXPECT(counts.flippedFromB >= 1);
}


// The schedule itself: the same seed gives the same fates, and a dropped byte is taken but never queued.
void TestSameSeedGivesTheSameFates() {
	LinkFaults first(DropPlan(77));
	LinkFaults second(DropPlan(77));
	for (uint8 value = 0; value < 64; value++) {
		uint8 a = value;
		uint8 b = value;
		SIM_EXPECT(first.Byte(a) == second.Byte(b));
		SIM_EXPECT(a == b);
	}

	SIM_EXPECT(first.Dropped() == second.Dropped());
	SIM_EXPECT(first.Dropped() >= 2);
}


void TestDroppedBytesAreTakenButNotQueued() {
	ByteQueue<32> queue;
	LinkFaults faults(LinkFaultPlan{1, 0, 0, 1ull << 1, 0});
	std::array<uint8, 4> bytes = {10, 20, 30, 40};
	SIM_EXPECT(PushThroughFaults(queue, faults, bytes) == 4);
	SIM_EXPECT(queue.Count() == 3);
	std::array<uint8, 3> out{};
	SIM_EXPECT(queue.Pop(out) == 3);
	SIM_EXPECT(out[0] == 10);
	SIM_EXPECT(out[1] == 30);
	SIM_EXPECT(out[2] == 40);
}


// A CRC with initial value 0 and no final XOR has a zero residue: the CRC of a packet with its own CRC at the end is 0, so the
// check of two packets joined as one passes when the second packet's check bytes are the joined CRC. A stream that loses a COBS
// delimiter joins two frames this way. Checksum::Crc16Modbus, with an initial value of 0xFFFF, rejects the same join. The loss chains use it.
template<typename Transform>
bool JoinedFramesPass() {
	std::array<uint8, 6> first = {1, 2, 3, 200, 0, 9};
	std::array<uint8, 5> second = {7, 0, 5, 44, 12};
	std::array<uint8, 8> firstFull{};
	std::array<uint8, 7> secondFull{};
	Transform transform;
	transform.Encode(VUB::Meta{}, first, firstFull);
	transform.Encode(VUB::Meta{}, second, secondFull);
	std::array<uint8, 15> joined{};
	for (std::size_t index = 0; index < firstFull.size(); index++) {
		joined[index] = firstFull[index];
	}

	for (std::size_t index = 0; index < secondFull.size(); index++) {
		joined[firstFull.size() + index] = secondFull[index];
	}

	return transform.Decode(VUB::Meta{}, joined).IsOk();
}


void TestZeroInitialPresetPassesJoinedFrames() {
	SIM_EXPECT(JoinedFramesPass<VUB::Crc<Checksum::Crc16Arc>>());
	SIM_EXPECT(!JoinedFramesPass<VUB::Crc<Checksum::Crc16Modbus>>());
}


int main() {
	TestSameSeedGivesTheSameFates();
	TestDroppedBytesAreTakenButNotQueued();
	TestZeroInitialPresetPassesJoinedFrames();
	TestUartG4DropsAreRecovered();
	TestUartG4FlippedBitsAreRecovered();
	TestUartEspDropsAreRecovered();
	TestCdcDropsAreRecovered();
	TestCdcFlippedBitsAreRecovered();
	TestBulkDropsAreRecovered();
	TestBulkFlippedBitsAreRecovered();
	TestHidDropsAreRecovered();
	TestHidFlippedBitsAreRecovered();
	return SimFinish("TestVubLinkLoss");
}
