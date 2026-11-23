#include "SimCheck.h"
#include "Chains.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Framing/Gap.h>
#include <Drivers/Interface/VUB/Framing/LengthPrefix.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The three framings in whole stacks, over the test kit's line. Each chain has the framing right above the link, and no other block,
// so a packet is the application's payload framed as it is. The line steps in time: each step sets the test clock, delivers the bytes
// that are due, and runs Process on both stacks.


// Payload number index: 1 to 20 bytes of a pattern that depends on the index, so a packet from another round does not match.
std::array<uint8, 40> Payload(std::size_t index, std::size_t& length) {
	std::array<uint8, 40> payload{};
	length = 1 + index % 20;
	for (std::size_t byte = 0; byte < length; byte++) {
		payload[byte] = static_cast<uint8>(index * 7 + byte * 3 + 1);
	}

	return payload;
}


// A stack on a line that does not report. COBS, a length byte, and the gap of 1000 us.
using CobsStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using LengthStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::LengthPrefix<1>>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using GapStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapUs(1000)
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same three on a line that reports its state, for the link change.
using ReportingCobsStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using ReportingLengthStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::LengthPrefix<1>>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using ReportingGapStream = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Gap>()
		.GapUs(1000)
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Hands the bytes a line delivers to a stack.
template<typename Stack>
struct StackSink {
	Stack* stack;


	void Feed(uint8 byte) {
		stack->Feed(byte);
	}
};


// One step of time: the clock moves to nowUs, the bytes due on the line reach their stacks, and both stacks run Process.
template<typename Stack, typename Line>
void Step(Stack& left, Stack& right, Line& line, uint32 nowUs) {
	testNowUs = nowUs;
	line.SetNow(nowUs);
	StackSink<Stack> toLeft{&left};
	StackSink<Stack> toRight{&right};
	line.DeliverToA(toLeft);
	line.DeliverToB(toRight);
	left.Process();
	right.Process();
}


// Sends count payloads from left to right, one per step of 5000 us, then runs a few idle steps so the last frame is complete. Every
// payload must come out, in order, exactly.
template<typename Stack, typename Line>
void SendClean(Stack& left, Stack& right, Line& line, std::size_t count) {
	uint32 now = 1000;
	for (std::size_t index = 0; index < count; index++) {
		std::size_t length = 0;
		std::array<uint8, 40> payload = Payload(index, length);
		SIM_EXPECT(left.Send(std::span<const uint8>(payload.data(), length)) == ResultStatus::ok);
		now += 5000;
		Step(left, right, line, now);
	}

	for (std::size_t idle = 0; idle < 3; idle++) {
		now += 5000;
		Step(left, right, line, now);
	}
}


// Checks that the receiver got the payloads 0 to count - 1, each exactly.
bool ReceivedAll(std::size_t count) {
	if (testReceivedCount != count) {
		return false;
	}

	for (std::size_t index = 0; index < count; index++) {
		std::size_t length = 0;
		std::array<uint8, 40> payload = Payload(index, length);
		if (testReceivedLength[index] != length) {
			return false;
		}

		for (std::size_t byte = 0; byte < length; byte++) {
			if (testReceived[index][byte] != payload[byte]) {
				return false;
			}
		}
	}

	return true;
}


// Twenty payloads over a clean line, with COBS: all come out exactly.
void TestCobsCleanLine() {
	ResetTestState();
	VUB::StreamLine<64, 512> line(VUB::Faults{});
	CobsStream left(line.a);
	CobsStream right(line.b);
	SendClean(left, right, line, 20);
	SIM_EXPECT(ReceivedAll(20));
}


// The same with a length byte.
void TestLengthCleanLine() {
	ResetTestState();
	VUB::StreamLine<64, 512> line(VUB::Faults{});
	LengthStream left(line.a);
	LengthStream right(line.b);
	SendClean(left, right, line, 20);
	SIM_EXPECT(ReceivedAll(20));
}


// The same with the gap framing: the silence between two steps ends each frame.
void TestGapCleanLine() {
	ResetTestState();
	VUB::StreamLine<64, 512> line(VUB::Faults{});
	GapStream left(line.a);
	GapStream right(line.b);
	SendClean(left, right, line, 20);
	SIM_EXPECT(ReceivedAll(20));
}


// A faulty line: ten per cent of the bytes are dropped. Each framing keeps running: no packet comes out that was not sent, with the COBS
// and the length byte, a damaged frame may come out as another packet of the same bytes, so the test only checks what the stack
// reports: packets come out, the drops are counted, and nothing overruns a buffer.
template<typename Stack>
void RunFaulty() {
	ResetTestState();
	VUB::Faults faults;
	faults.dropPercent = 10;
	faults.seed = 7;
	VUB::StreamLine<64, 512> line(faults);
	Stack left(line.a);
	Stack right(line.b);
	SendClean(left, right, line, 100);
	SIM_EXPECT(testReceivedCount <= 100);
	SIM_EXPECT(testReceivedCount > 0);
}


void TestCobsFaultyLine() {
	RunFaulty<CobsStream>();
}


void TestLengthFaultyLine() {
	RunFaulty<LengthStream>();
}


void TestGapFaultyLine() {
	RunFaulty<GapStream>();
}


// COBS on a line that reports its state: a frame cut in the middle by a link change is dropped. The first two bytes of 11 22 33 44
// (frame 05 11 22 33 44 00) reach the stack, the link goes down and up, and the rest, 22 33 44 00, is read as a broken frame. The next
// clean payload comes out.
void TestCobsResetOnLinkChange() {
	ResetTestState();
	VUB::StreamLine<64, 512, true> line(VUB::Faults{});
	ReportingCobsStream left(line.a);
	ReportingCobsStream right(line.b);
	testNowUs = 1000;
	right.Feed(0x05);
	right.Feed(0x11);
	right.Process();
	line.SetLinkUp(false);
	right.Process();
	line.SetLinkUp(true);
	right.Process();
	right.Feed(0x22);
	right.Feed(0x33);
	right.Feed(0x44);
	right.Feed(0x00);
	right.Process();
	SIM_EXPECT(testReceivedCount == 0);
	const std::array<uint8, 1> next = {0x55};
	SIM_EXPECT(left.Send(std::span<const uint8>(next.data(), next.size())) == ResultStatus::ok);
	Step(left, right, line, 2000);
	Step(left, right, line, 3000);
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 1);
	SIM_EXPECT(testReceived[0][0] == 0x55);
}


// The length byte on a reporting line: the frame A5 04 57 11 22 33 44 cut after 11 by a link change. The rest, 22 33 44, has no sync byte
// and is dropped; the clean packet after it comes out.
void TestLengthResetOnLinkChange() {
	ResetTestState();
	VUB::StreamLine<64, 512, true> line(VUB::Faults{});
	ReportingLengthStream left(line.a);
	ReportingLengthStream right(line.b);
	testNowUs = 1000;
	right.Feed(0xA5);
	right.Feed(0x04);
	right.Feed(0x57);
	right.Feed(0x11);
	right.Process();
	line.SetLinkUp(false);
	right.Process();
	line.SetLinkUp(true);
	right.Process();
	right.Feed(0x22);
	right.Feed(0x33);
	right.Feed(0x44);
	right.Process();
	SIM_EXPECT(testReceivedCount == 0);
	const std::array<uint8, 2> next = {0x66, 0x77};
	SIM_EXPECT(left.Send(std::span<const uint8>(next.data(), next.size())) == ResultStatus::ok);
	Step(left, right, line, 2000);
	Step(left, right, line, 3000);
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x66);
}


// The gap framing on a reporting line: 11 22 is received at 1000 and 1100, the link goes down and up, then 33 44 comes at 1200. The
// link change drops 11 22, so the packet that comes out after the silence is 33 44.
void TestGapResetOnLinkChange() {
	ResetTestState();
	VUB::StreamLine<64, 512, true> line(VUB::Faults{});
	ReportingGapStream left(line.a);
	ReportingGapStream right(line.b);
	testNowUs = 1000;
	right.Feed(0x11);
	testNowUs = 1100;
	right.Feed(0x22);
	right.Process();
	line.SetLinkUp(false);
	right.Process();
	line.SetLinkUp(true);
	right.Process();
	testNowUs = 1200;
	right.Feed(0x33);
	right.Feed(0x44);
	right.Process();
	testNowUs = 7000;
	right.Process();
	SIM_EXPECT(testReceivedCount == 1);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x33);
	SIM_EXPECT(testReceived[0][1] == 0x44);
}


// Two packets queued at the same time go out with the gap between them: the first is sent at 1000, and the second waits until the gap
// has passed since its last byte, so it goes out at 2000. Its bytes arrive at 2500, a gap after the first packet's last byte at 1500,
// so the two come out as two packets. Without the wait they would arrive together and run into one frame.
void TestGapSendWaitsForGap() {
	ResetTestState();
	VUB::StreamLine<64, 512> line(VUB::Faults{});
	GapStream left(line.a);
	GapStream right(line.b);
	const std::array<uint8, 2> first = {0x11, 0x22};
	const std::array<uint8, 2> second = {0x33, 0x44};
	SIM_EXPECT(left.Send(std::span<const uint8>(first.data(), first.size())) == ResultStatus::ok);
	SIM_EXPECT(left.Send(std::span<const uint8>(second.data(), second.size())) == ResultStatus::ok);
	Step(left, right, line, 1000);
	Step(left, right, line, 1500);
	Step(left, right, line, 2000);
	Step(left, right, line, 2500);
	Step(left, right, line, 3000);
	Step(left, right, line, 4000);
	SIM_EXPECT(testReceivedCount == 2);
	SIM_EXPECT(testReceivedLength[0] == 2);
	SIM_EXPECT(testReceived[0][0] == 0x11);
	SIM_EXPECT(testReceived[0][1] == 0x22);
	SIM_EXPECT(testReceivedLength[1] == 2);
	SIM_EXPECT(testReceived[1][0] == 0x33);
	SIM_EXPECT(testReceived[1][1] == 0x44);
}


int main() {
	TestCobsCleanLine();
	TestLengthCleanLine();
	TestGapCleanLine();
	TestCobsFaultyLine();
	TestLengthFaultyLine();
	TestGapFaultyLine();
	TestCobsResetOnLinkChange();
	TestLengthResetOnLinkChange();
	TestGapResetOnLinkChange();
	TestGapSendWaitsForGap();
	return SimFinish("TestVubFramingStacks");
}
