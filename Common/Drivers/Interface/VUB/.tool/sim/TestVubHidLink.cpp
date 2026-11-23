#include "SimCheck.h"
#include "LinkKit.h"
#include "UsbDoubles.h"
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Link/HidLink.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// The HID link over a HID function (FakeHid, 64-byte reports, two in the queue). A packet link with no framing: each report is one
// packet, and no framing is put above it.


using HidDouble = FakeHid<64, 2>;
using HidLinkType = VUB::HidLink<HidDouble, 64>;


static_assert(VUB::PacketLink<HidLinkType>);
static_assert(VUB::ReceivingLink<HidLinkType>);
static_assert(!VUB::PollingLink<HidLinkType>);
static_assert(!VUB::ReportingLink<HidLinkType>);
static_assert(!VUB::StreamLink<HidLinkType>);
static_assert(HidLinkType::traits.framed);
static_assert(!HidLinkType::traits.reliable);
static_assert(HidLinkType::traits.duplex == VUB::Duplex::Full);
static_assert(HidLinkType::traits.maxPacket == 63);


// A packet of 3 bytes goes in one report: the length byte 3, the payload, and zero padding to the report's 64 bytes.
void TestSendPacksTheReportByteByByte() {
	HidDouble hid;
	HidLinkType link(hid);
	hid.SetConfigured(true);

	std::array<uint8, 3> payload = {0xA1, 0xB2, 0xC3};
	SIM_EXPECT(link.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(hid.reportsSent == 1);
	SIM_EXPECT(hid.lastReport[0] == 3);
	SIM_EXPECT(hid.lastReport[1] == 0xA1);
	SIM_EXPECT(hid.lastReport[2] == 0xB2);
	SIM_EXPECT(hid.lastReport[3] == 0xC3);
	for (std::size_t index = 4; index < 64; index++) {
		SIM_EXPECT(hid.lastReport[index] == 0);
	}
}


// A payload of 63 bytes fills the report; a payload of 64 bytes or none is refused with invalidParameter, and sends nothing.
void TestSendRefusesPayloadsThatDoNotFit() {
	HidDouble hid;
	HidLinkType link(hid);
	hid.SetConfigured(true);

	std::array<uint8, 63> fits{};
	fits[62] = 0x77;
	SIM_EXPECT(link.Send(fits) == ResultStatus::ok);
	SIM_EXPECT(hid.lastReport[0] == 63);
	SIM_EXPECT(hid.lastReport[63] == 0x77);

	std::array<uint8, 64> tooLong{};
	SIM_EXPECT(link.Send(tooLong) == ResultStatus::invalidParameter);
	std::span<const uint8> none;
	SIM_EXPECT(link.Send(none) == ResultStatus::invalidParameter);
	SIM_EXPECT(hid.reportsSent == 1);
}


// Before configuration the function says notReady, and the link reports busy so the stack keeps the packet. A full queue of reports is
// busy too, and the packet goes once a report leaves the queue.
void TestNotReadyAndFullQueueAreBusy() {
	HidDouble hid;
	HidLinkType link(hid);
	std::array<uint8, 2> payload = {5, 6};
	SIM_EXPECT(link.Send(payload) == ResultStatus::busy);
	SIM_EXPECT(hid.reportsSent == 0);

	hid.SetConfigured(true);
	SIM_EXPECT(link.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(link.Send(payload) == ResultStatus::busy);

	std::array<uint8, 64> report{};
	SIM_EXPECT(hid.HostTake(report));
	SIM_EXPECT(link.Send(payload) == ResultStatus::ok);
	SIM_EXPECT(hid.reportsSent == 3);
}


// SetReceiver sets the output delegate. An output report with a payload goes to the receiver as a packet. A report whose length byte is
// zero or larger than the report allows is dropped.
void TestOutputReportsFeedThePacketsAndDropTheRest() {
	HidDouble hid;
	HidLinkType link(hid);
	FeedRecord record;
	link.SetReceiver(RecordReceiver(record));
	SIM_EXPECT(link.ReceiveStatus() == ResultStatus::ok);
	SIM_EXPECT(hid.HasOutputDelegate());

	std::array<uint8, 64> report{};
	report[0] = 2;
	report[1] = 0xAA;
	report[2] = 0xBB;
	hid.DeliverOutput(report);
	SIM_EXPECT(record.packetCount == 1);
	SIM_EXPECT(record.packetLengths[0] == 2);

	std::array<uint8, 64> empty{};
	hid.DeliverOutput(empty);
	SIM_EXPECT(record.packetCount == 1);

	std::array<uint8, 64> oversized{};
	oversized[0] = 64;
	hid.DeliverOutput(oversized);
	SIM_EXPECT(record.packetCount == 1);

	std::array<uint8, 64> full{};
	full[0] = 63;
	hid.DeliverOutput(full);
	SIM_EXPECT(record.packetCount == 2);
	SIM_EXPECT(record.packetLengths[1] == 63);
}


// Taking the receiver back clears the output delegate, so an output report reaches nothing.
void TestTakingTheReceiverBackClearsTheDelegate() {
	HidDouble hid;
	HidLinkType link(hid);
	FeedRecord record;
	link.SetReceiver(RecordReceiver(record));
	link.SetReceiver(VUB::Receiver());
	SIM_EXPECT(!hid.HasOutputDelegate());

	std::array<uint8, 64> report{};
	report[0] = 1;
	hid.DeliverOutput(report);
	SIM_EXPECT(record.packetCount == 0);
}


// The stack over the link has no framing: each packet is one report. The queue of reports holds two, so most packets wait in the stack
// until the host has taken the reports before them.
using HidStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<HidLinkType>()
		.MaxPayload(40)
		.SendQueue(8)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtA>();
}>;


using HidStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<HidLinkType>()
		.MaxPayload(40)
		.SendQueue(8)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtB>();
}>;


// Moves every input report that the host side of from has, into the output of to, as the host sends them.
void PumpReports(HidDouble& from, HidDouble& to) {
	std::array<uint8, 64> report{};
	while (from.HostTake(report)) {
		to.DeliverOutput(report);
	}
}


void TestStacksExchangeOverHidWithBusyReports() {
	ResetLinkKit();
	HidDouble hidA;
	HidDouble hidB;
	HidLinkType linkA(hidA);
	HidLinkType linkB(hidB);
	HidStackA stackA(linkA);
	HidStackB stackB(linkB);
	hidA.SetConfigured(true);
	hidB.SetConfigured(true);

	for (uint8 id = 0; id < 6; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(stackA.Send(payload) == ResultStatus::ok);
	}

	stackA.Process();
	SIM_EXPECT(hidA.reportsSent == 2);

	for (int round = 0; round < 10; round++) {
		stackA.Process();
		PumpReports(hidA, hidB);
		stackB.Process();
		PumpReports(hidB, hidA);
	}

	SIM_EXPECT(logAtB.count == 6);
	for (uint8 id = 0; id < 6; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}

	SIM_EXPECT(stackA.Drops().sendError == 0);
	SIM_EXPECT(stackB.Drops().queueFull == 0);
}


// The delivery layer over the HID link.
using ReliableHidStackA = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Link<HidLinkType>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtA>()
		.Clock<&LinkClock>();
}>;


using ReliableHidStackB = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40)
		.Link<HidLinkType>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReceiveAtB>()
		.Clock<&LinkClock>();
}>;


void TestReliableStackOverHid() {
	ResetLinkKit();
	HidDouble hidA;
	HidDouble hidB;
	HidLinkType linkA(hidA);
	HidLinkType linkB(hidB);
	ReliableHidStackA stackA(linkA);
	ReliableHidStackB stackB(linkB);
	stackA.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	stackB.Find<VUB::ReliableOf<40, 4>>().SetSession(0x1234);
	hidA.SetConfigured(true);
	hidB.SetConfigured(true);

	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		int attempts = 0;
		while (stackA.Send(payload) != ResultStatus::ok && attempts < 100) {
			stackA.Process();
			PumpReports(hidA, hidB);
			stackB.Process();
			PumpReports(hidB, hidA);
			linkNowUs += 1000;
			attempts++;
		}

		SIM_EXPECT(attempts < 100);
	}

	for (int round = 0; round < 200; round++) {
		stackA.Process();
		PumpReports(hidA, hidB);
		stackB.Process();
		PumpReports(hidB, hidA);
		linkNowUs += 1000;
	}

	SIM_EXPECT(logAtB.count == 4);
	for (uint8 id = 0; id < 4; id++) {
		std::array<uint8, 24> payload = LinkPayload(id);
		SIM_EXPECT(logAtB.Is(id, payload));
	}
}


int main() {
	TestSendPacksTheReportByteByByte();
	TestSendRefusesPayloadsThatDoNotFit();
	TestNotReadyAndFullQueueAreBusy();
	TestOutputReportsFeedThePacketsAndDropTheRest();
	TestTakingTheReceiverBackClearsTheDelegate();
	TestStacksExchangeOverHidWithBusyReports();
	TestReliableStackOverHid();
	return SimFinish("TestVubHidLink");
}
