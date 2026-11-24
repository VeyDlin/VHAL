#include "SimCheck.h"
#include "LinkKit.h"
#include "UartDoubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/UartLink.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The UART link over the real UARTAdapter base (FakeUart). Each test has two ports, a peer each, and the links on them.


using UartLinkType = VUB::UartLink<FakeUart>;
using SmallUartLink = VUB::UartLink<FakeUart, 8>;


// The traits and the concepts: a stream link that receives in an interrupt, and is neither polled nor reporting.
static_assert(VUB::StreamLink<UartLinkType>);
static_assert(VUB::ReceivingLink<UartLinkType>);
static_assert(!VUB::PollingLink<UartLinkType>);
static_assert(!VUB::ReportingLink<UartLinkType>);
static_assert(!VUB::PacketLink<UartLinkType>);
static_assert(!UartLinkType::traits.framed);
static_assert(!UartLinkType::traits.reliable);
static_assert(!UartLinkType::traits.reportsState);
static_assert(UartLinkType::traits.duplex == VUB::Duplex::Full);
static_assert(UartLinkType::traits.starts == VUB::Initiator::Either);
static_assert(UartLinkType::traits.maxPacket == 64);


// The receiver is attached only when it has a stack behind it.
void TestReceiverAttachedOnlyWhenGiven() {
	FeedRecord record;
	SIM_EXPECT(!VUB::Receiver().Attached());
	SIM_EXPECT(RecordReceiver(record).Attached());
}


// Write copies up to TxBytes into the link's buffer and starts one transfer. A second Write while it runs returns 0, and the bytes
// go out once the transfer ends.
void TestWriteCopiesUpToTxBytesAndWaitsForTheTransfer() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	SmallUartLink linkA(portA);
	SmallUartLink linkB(portB);
	FeedRecord recordB;
	linkB.SetReceiver(RecordReceiver(recordB));

	std::array<uint8, 12> data{};
	for (std::size_t index = 0; index < data.size(); index++) {
		data[index] = static_cast<uint8>(index + 1);
	}

	Result<std::size_t> first = linkA.Write(data);
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(first.Value() == 8);
	SIM_EXPECT(portA.transfersStarted == 1);

	Result<std::size_t> second = linkA.Write(data);
	SIM_EXPECT(second.IsOk());
	SIM_EXPECT(second.Value() == 0);
	SIM_EXPECT(portA.transfersStarted == 1);
	SIM_EXPECT(recordB.byteCount == 0);

	portA.FinishTransmit();
	SIM_EXPECT(recordB.byteCount == 8);
	for (std::size_t index = 0; index < 8; index++) {
		SIM_EXPECT(recordB.bytes[index] == static_cast<uint8>(index + 1));
	}

	Result<std::size_t> third = linkA.Write(std::span<const uint8>(data).subspan(8));
	SIM_EXPECT(third.IsOk());
	SIM_EXPECT(third.Value() == 4);
	portA.FinishTransmit();
	SIM_EXPECT(recordB.byteCount == 12);
	SIM_EXPECT(recordB.bytes[11] == 12);
}


// The ESP32 port raises Irq::Tx before WriteArrayAsync returns. The link has marked its transfer as running before the call, so the
// completion ends it, and the next Write is accepted at once.
void TestEspPortCompletesInsideWrite() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	portA.completesInWrite = true;
	portB.completesInWrite = true;
	SmallUartLink linkA(portA);
	SmallUartLink linkB(portB);
	FeedRecord recordB;
	linkB.SetReceiver(RecordReceiver(recordB));

	std::array<uint8, 5> data = {1, 2, 3, 4, 5};
	Result<std::size_t> first = linkA.Write(data);
	SIM_EXPECT(first.IsOk());
	SIM_EXPECT(first.Value() == 5);
	SIM_EXPECT(recordB.byteCount == 5);

	Result<std::size_t> second = linkA.Write(data);
	SIM_EXPECT(second.IsOk());
	SIM_EXPECT(second.Value() == 5);
	SIM_EXPECT(portA.transfersStarted == 2);
	SIM_EXPECT(recordB.byteCount == 10);
}


// A transfer the port started outside the link makes the link's Write busy: it returns 0 and does not hold the link.
void TestBusyPortGivesZeroAndDoesNotHoldTheLink() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	SmallUartLink linkA(portA);

	std::array<uint8, 4> outside = {9, 9, 9, 9};
	SIM_EXPECT(portA.WriteArrayAsync(outside.data(), 4) == ResultStatus::ok);

	std::array<uint8, 4> data = {1, 2, 3, 4};
	Result<std::size_t> refused = linkA.Write(data);
	SIM_EXPECT(refused.IsOk());
	SIM_EXPECT(refused.Value() == 0);

	portA.FinishTransmit();
	Result<std::size_t> accepted = linkA.Write(data);
	SIM_EXPECT(accepted.IsOk());
	SIM_EXPECT(accepted.Value() == 4);
}


// SetReceiver starts the continuous receive and routes each byte of the receive interrupt into the receiver, one byte at a time.
void TestReceiveFeedsEachByteOfTheInterrupt() {
	FakeUart port;
	UartLinkType link(port);
	FeedRecord record;
	link.SetReceiver(RecordReceiver(record));
	SIM_EXPECT(link.ReceiveStatus() == ResultStatus::ok);
	SIM_EXPECT(port.rxStarts == 1);
	SIM_EXPECT(port.onInterrupt != nullptr);

	port.RaiseRx(0x41);
	port.RaiseRx(0x00);
	port.RaiseRx(0x42);
	SIM_EXPECT(record.byteCount == 3);
	SIM_EXPECT(record.bytes[0] == 0x41);
	SIM_EXPECT(record.bytes[1] == 0x00);
	SIM_EXPECT(record.bytes[2] == 0x42);
}


// Receive errors are counted per kind, and read with Errors().
void TestReceiveErrorsAreCountedPerKind() {
	FakeUart port;
	UartLinkType link(port);
	FeedRecord record;
	link.SetReceiver(RecordReceiver(record));

	port.RaiseError(FakeUart::Error::Parity);
	port.RaiseError(FakeUart::Error::Noise);
	port.RaiseError(FakeUart::Error::Frame);
	port.RaiseError(FakeUart::Error::Overrun);
	port.RaiseError(FakeUart::Error::Overrun);
	port.RaiseError(FakeUart::Error::Overrun);

	VUB::UartErrorCounts counts = link.Errors();
	SIM_EXPECT(counts.parity == 1);
	SIM_EXPECT(counts.noise == 1);
	SIM_EXPECT(counts.frame == 1);
	SIM_EXPECT(counts.overrun == 3);
	SIM_EXPECT(record.byteCount == 0);
}


// Taking the receiver back stops the continuous receive and clears both callbacks of the port.
void TestTakingTheReceiverBackStopsReceiveAndClearsCallbacks() {
	FakeUart port;
	UartLinkType link(port);
	FeedRecord record;
	link.SetReceiver(RecordReceiver(record));
	link.SetReceiver(VUB::Receiver());
	SIM_EXPECT(port.rxStops == 1);
	SIM_EXPECT(port.onInterrupt == nullptr);
	SIM_EXPECT(port.onError == nullptr);

	port.RaiseRx(0x55);
	SIM_EXPECT(record.byteCount == 0);
}


// A byte that the peer sends while this port's continuous receive is off is lost, as on a UART with no receiver.
void TestBytesToAPortWithReceiveOffAreLost() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	SmallUartLink linkA(portA);
	std::array<uint8, 3> data = {7, 8, 9};
	SIM_EXPECT(linkA.Write(data).Value() == 3);
	portA.FinishTransmit();
	SIM_EXPECT(portB.bytesLost == 3);
}


// The stack runs over the link with COBS. Two stacks, one per port, exchange payloads in both directions. The rounds go: each stack
// processes, then each port finishes its transfer, so the bytes reach the peer's receive interrupt.
using UartStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<UartLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtA>();
}>;


using UartStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<UartLinkType>()
		.MaxPayload(32)
		.OnReceive<&ReceiveAtB>();
}>;


// One round of the G4 port: the stacks process, and each port finishes the transfer it started.
template<typename StackTypeA, typename StackTypeB>
void G4Round(StackTypeA& stackA, FakeUart& portA, StackTypeB& stackB, FakeUart& portB) {
	stackA.Process();
	portA.FinishTransmit();
	stackB.Process();
	portB.FinishTransmit();
}


// Both directions with COBS on the G4 port (a transfer ends only on FinishTransmit). Payloads with zero bytes come out whole and in order.
void TestStacksExchangeOverTheG4PortWithCobs() {
	ResetLinkKit();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	UartStackA stackA(linkA);
	UartStackB stackB(linkB);

	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
		std::array<uint8, 24> reply = LinkPayload(static_cast<uint8>(id + 100));
		SIM_EXPECT(stackB.Send(reply) == ResultStatus::ok);
	}

	for (int round = 0; round < 40; round++) {
		G4Round(stackA, portA, stackB, portB);
	}

	SIM_EXPECT(logAtB.count == 4);
	SIM_EXPECT(logAtA.count == 4);
	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
		std::array<uint8, 24> reply = LinkPayload(static_cast<uint8>(id + 100));
		SIM_EXPECT(logAtA.Is(id, reply));
	}

	SIM_EXPECT(stackA.Drops().sendError == 0);
	SIM_EXPECT(stackB.Drops().sendError == 0);
}


// A busy transfer keeps the packets in the stack. Three packets queued: the first Process starts one transfer, the other two wait
// in the stack, and each goes out when the transfer before it has ended. Every packet arrives once and in order.
void TestBusyTransferKeepsThePacketsInTheStack() {
	ResetLinkKit();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	UartStackA stackA(linkA);
	UartStackB stackB(linkB);

	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	}

	stackA.Process();
	SIM_EXPECT(portA.transfersStarted == 1);
	SIM_EXPECT(logAtB.count == 0);

	for (int round = 0; round < 40; round++) {
		G4Round(stackA, portA, stackB, portB);
	}

	SIM_EXPECT(portA.transfersStarted == 3);
	SIM_EXPECT(logAtB.count == 3);
	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}

	SIM_EXPECT(stackA.Drops().sendError == 0);
}


// The ESP32 port: the transfer completes inside Write, so one Process sends every packet, and the peer receives them at once.
void TestStacksExchangeOverTheEspPort() {
	ResetLinkKit();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	portA.completesInWrite = true;
	portB.completesInWrite = true;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	UartStackA stackA(linkA);
	UartStackB stackB(linkB);

	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	}

	stackA.Process();
	stackB.Process();
	SIM_EXPECT(logAtB.count == 3);
	for (uint8 id = 0; id < 3; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}
}


// The delivery layer on top of the UART link. The first Send returns busy while the session starts, so the sends repeat until ok.
using ReliableUartStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<UartLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using ReliableUartStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Framing<VUB::Cobs>()
		.Link<UartLinkType>()
		.MaxPayload(40)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


// VUB::Reliable over the G4 port: every payload arrives once, in order, with the transfers in between.
void TestReliableStackOverTheG4Port() {
	ResetLinkKit();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	ReliableUartStackA stackA(linkA);
	ReliableUartStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		int attempts = 0;
		while (stackA.Send(payload) != ResultStatus::ok && attempts < 100) {
			G4Round(stackA, portA, stackB, portB);
			linkNowUs += 1000;
			attempts++;
		}

		SIM_EXPECT(attempts < 100);
	}

	for (int round = 0; round < 200; round++) {
		G4Round(stackA, portA, stackB, portB);
		linkNowUs += 1000;
	}

	SIM_EXPECT(logAtB.count == 4);
	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}
}


// The same over the ESP32 port.
void TestReliableStackOverTheEspPort() {
	ResetLinkKit();
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	portA.completesInWrite = true;
	portB.completesInWrite = true;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	ReliableUartStackA stackA(linkA);
	ReliableUartStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);

	std::array<uint8, 24> first = LinkPayload(7);
	int attempts = 0;
	while (stackA.Send(first) != ResultStatus::ok && attempts < 100) {
		stackA.Process();
		stackB.Process();
		linkNowUs += 1000;
		attempts++;
	}

	for (int round = 0; round < 20; round++) {
		stackA.Process();
		stackB.Process();
		linkNowUs += 1000;
	}

	SIM_EXPECT(logAtB.count == 1);
	SIM_EXPECT(logAtB.Is(0, first));
}


// A transfer that runs when the receiver is taken back ends after the detach, and the next Write goes out. The port's Irq::Tx is
// still handled, so the busy mark clears.
void TestDetachWhileATransferRunsLetsTheNextWriteGoOut() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	FeedRecord recordA;
	FeedRecord recordB;
	linkA.SetReceiver(RecordReceiver(recordA));
	linkB.SetReceiver(RecordReceiver(recordB));

	std::array<uint8, 4> first = {1, 2, 3, 4};
	SIM_EXPECT(linkA.Write(first).Value() == 4);
	linkA.SetReceiver(VUB::Receiver());

	portA.FinishTransmit();
	std::array<uint8, 4> second = {5, 6, 7, 8};
	SIM_EXPECT(linkA.Write(second).Value() == 4);
	SIM_EXPECT(portA.transfersStarted == 2);
	portA.FinishTransmit();
	SIM_EXPECT(recordB.byteCount == 8);
	SIM_EXPECT(recordB.bytes[0] == 1);
	SIM_EXPECT(recordB.bytes[7] == 8);
}


// The same on the ESP32 port: the transfer ends inside Write, so the detach finds the link idle and the next Write runs again.
void TestDetachAfterAnEspTransferLetsTheNextWriteGoOut() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	portA.completesInWrite = true;
	portB.completesInWrite = true;
	UartLinkType linkA(portA);
	UartLinkType linkB(portB);
	FeedRecord recordA;
	FeedRecord recordB;
	linkA.SetReceiver(RecordReceiver(recordA));
	linkB.SetReceiver(RecordReceiver(recordB));

	std::array<uint8, 4> first = {1, 2, 3, 4};
	SIM_EXPECT(linkA.Write(first).Value() == 4);
	linkA.SetReceiver(VUB::Receiver());
	SIM_EXPECT(portA.onInterrupt == nullptr);

	std::array<uint8, 4> second = {5, 6, 7, 8};
	SIM_EXPECT(linkA.Write(second).Value() == 4);
	SIM_EXPECT(portA.transfersStarted == 2);
	SIM_EXPECT(recordB.byteCount == 8);
}


// A link that goes while its transfer runs stops the transfer and clears the port's callbacks, so the port calls nothing of it.
void TestLinkGoneWhileATransferRunsLeavesThePortClear() {
	FakeUart portA;
	FakeUart portB;
	portA.peer = &portB;
	portB.peer = &portA;
	{
		UartLinkType linkA(portA);
		FeedRecord recordA;
		linkA.SetReceiver(RecordReceiver(recordA));
		std::array<uint8, 4> bytes = {1, 2, 3, 4};
		SIM_EXPECT(linkA.Write(bytes).Value() == 4);
		linkA.SetReceiver(VUB::Receiver());
		SIM_EXPECT(portA.onInterrupt != nullptr);
	}

	SIM_EXPECT(portA.transfersAborted == 1);
	SIM_EXPECT(portA.onInterrupt == nullptr);
	SIM_EXPECT(portA.onError == nullptr);
	portA.FinishTransmit();
	SIM_EXPECT(portB.bytesLost == 0);
}


int main() {
	TestLinkGoneWhileATransferRunsLeavesThePortClear();
	TestDetachWhileATransferRunsLetsTheNextWriteGoOut();
	TestDetachAfterAnEspTransferLetsTheNextWriteGoOut();
	TestReceiverAttachedOnlyWhenGiven();
	TestWriteCopiesUpToTxBytesAndWaitsForTheTransfer();
	TestEspPortCompletesInsideWrite();
	TestBusyPortGivesZeroAndDoesNotHoldTheLink();
	TestReceiveFeedsEachByteOfTheInterrupt();
	TestReceiveErrorsAreCountedPerKind();
	TestTakingTheReceiverBackStopsReceiveAndClearsCallbacks();
	TestBytesToAPortWithReceiveOffAreLost();
	TestStacksExchangeOverTheG4PortWithCobs();
	TestBusyTransferKeepsThePacketsInTheStack();
	TestStacksExchangeOverTheEspPort();
	TestReliableStackOverTheG4Port();
	TestReliableStackOverTheEspPort();
	return SimFinish("TestVubUartLink");
}
