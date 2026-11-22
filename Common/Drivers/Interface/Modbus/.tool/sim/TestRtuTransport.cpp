#include "SimCheck.h"
#include "FakeGpio.h"
#include "FakeLine.h"
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <array>
#include <cstddef>
#include <span>
#include <vector>


// Expected values are hand-worked for 9600 baud, 11 bits a character: one character is 1145 us, t1.5 is 1718 us, t3.5 is 4010 us,
// and two characters of turnaround are 2291 us. The simulated clock advances 1 us a read, so a loop that waits for a time ends
// on the first read at or after it.
constexpr uint64 CharacterUs = 1145;
constexpr uint64 InterFrameUs = 4010;
constexpr uint64 TurnaroundUs = 2291;

// The time a Send may wait for the line to go quiet. Every send in these tests finds the line quiet, so the wait is never reached.
constexpr uint32 SendWaitUs = 1000000;


uint32 DeadClock() {
	return 0;
}


using Link = RtuTransport<FakeLine, FakeGpio, +[] { return RtuBuilder().Clock<&SimClockUs>(); }>;
using LenientLink = RtuTransport<FakeLine, FakeGpio, +[] { return RtuBuilder().Lenient().Clock<&SimClockUs>(); }>;
using DeadLink = RtuTransport<FakeLine, FakeGpio, +[] { return RtuBuilder().Clock<&DeadClock>(); }>;


// The simulated time starts at a value of the test's choosing, and the line runs at 9600 baud, 8N1.
void ResetSim(uint64 startUs) {
	simNowUs = startUs;
	simCriticalDepth = 0;
}


void SetLine(FakeLine& line, uint32 baud) {
	FakeLine::Parameters parameters;
	parameters.baudRate = baud;
	line.SetParameters(parameters);
}


// Schedules the bytes of one chunk of a frame: the first at startUs, then one character time apart. Returns the time of the last byte.
uint64 ScheduleChunk(FakeLine& line, uint64 startUs, std::span<const uint8> bytes, uint64 spacingUs) {
	uint64 timeUs = startUs;

	for (std::size_t index = 0; index < bytes.size(); index++) {
		line.ScheduleByte(timeUs, bytes[index]);

		if (index + 1 < bytes.size()) {
			timeUs += spacingUs;
		}
	}

	return timeUs;
}


// A 9-byte reply, read holding registers of two registers from slave 1, with the CRC from the library.
std::array<uint8, 9> NineByteReply() {
	std::array<uint8, 7> body = {0x01, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x0B};
	uint16 crc = Crc::Calculate(body.data(), body.size(), Crc::CRC_16_MODBUS());
	return {0x01, 0x03, 0x04, 0x00, 0x0A, 0x00, 0x0B, static_cast<uint8>(crc & 0xFF), static_cast<uint8>(crc >> 8)};
}


// An 8-byte write request (function 06) from slave 1, with the CRC from the library.
std::array<uint8, 8> EightByteRequest() {
	std::array<uint8, 6> body = {0x01, 0x06, 0x00, 0x01, 0x00, 0x03};
	uint16 crc = Crc::Calculate(body.data(), body.size(), Crc::CRC_16_MODBUS());
	return {0x01, 0x06, 0x00, 0x01, 0x00, 0x03, static_cast<uint8>(crc & 0xFF), static_cast<uint8>(crc >> 8)};
}


// The clock starts ordinary, and a transport with a live clock starts. Used by most tests.
bool BeginLive(Link& link) {
	return link.Begin() == ResultStatus::ok;
}


void TestBeginChecksClock() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);

	// A clock that never advances fails at setup, so no gap is ever measured with it.
	DeadLink dead(line);
	SIM_EXPECT(dead.Begin() == ResultStatus::error);

	ResetSim(1000000);
	Link live(line);
	SIM_EXPECT(live.Begin() == ResultStatus::ok);
}


void TestBeginNeedsBaudAndStart() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 0);
	Link zeroBaud(line);
	SIM_EXPECT(zeroBaud.Begin() == ResultStatus::invalidParameter);

	SetLine(line, 9600);
	Link unstarted(line);
	std::array<uint8, 4> frame = {1, 2, 3, 4};
	std::array<uint8, 8> buffer{};
	SIM_EXPECT(unstarted.Send(frame, SendWaitUs) == ResultStatus::noInit);
	SIM_EXPECT(unstarted.Receive(buffer, 0).Error() == ResultStatus::noInit);
}


// Send waits for t3.5 of silence after the last byte. The first byte leaves at lastByte + t3.5 + 1 and one character later it has ended.
void TestSendWaitsForSilence() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	uint64 lastByte = simNowUs + 500;
	line.ScheduleByte(lastByte, 0xAA);

	std::array<uint8, 4> frame = {1, 2, 3, 4};
	SIM_EXPECT(link.Send(frame, SendWaitUs) == ResultStatus::ok);
	SIM_EXPECT(line.Sent().size() == 4);

	uint64 firstStart = line.Sent().front().endUs - CharacterUs;
	SIM_EXPECT(firstStart == lastByte + InterFrameUs + 1);
}


// The three parity and stop bit settings all give 11 bits a character, so they give the same gaps.
void TestTimingIndependentOfFormat() {
	for (int setting = 0; setting < 3; setting++) {
		ResetSim(1000000);
		FakeLine line;
		FakeLine::Parameters parameters;
		parameters.baudRate = 9600;
		if (setting == 1) {
			parameters.parity = FakeLine::Parity::Even;
		}
		if (setting == 2) {
			parameters.stopBits = FakeLine::StopBits::B2;
		}
		line.SetParameters(parameters);

		Link link(line);
		SIM_EXPECT(BeginLive(link));

		uint64 lastByte = simNowUs + 500;
		line.ScheduleByte(lastByte, 0x55);
		std::array<uint8, 4> frame = {1, 2, 3, 4};
		link.Send(frame, SendWaitUs);
		SIM_EXPECT(line.Sent().front().endUs - CharacterUs == lastByte + InterFrameUs + 1);
	}
}


// A slave answers only after the request has been silent for t3.5: Receive returns the request after that silence, and the reply
// starts no earlier than t3.5 after the request's last byte.
void TestReplyStartsAfterSilence() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link slave(line);
	SIM_EXPECT(BeginLive(slave));

	std::array<uint8, 8> request = EightByteRequest();
	uint64 lastByte = ScheduleChunk(line, simNowUs + 1000, request, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> received = slave.Receive(buffer, 1000000);
	SIM_EXPECT(received.IsOk() && received.Value() == 8);
	SIM_EXPECT(simNowUs >= lastByte + InterFrameUs + 1);

	std::array<uint8, 9> reply = NineByteReply();
	SIM_EXPECT(slave.Send(reply, SendWaitUs) == ResultStatus::ok);
	SIM_EXPECT(line.Sent().front().endUs - CharacterUs >= lastByte + InterFrameUs);
}


// Nine bytes in three chunks, each gap below t1.5, are one frame.
void TestChunksBelowT15AreOneFrame() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	std::array<uint8, 3> first = {reply[0], reply[1], reply[2]};
	std::array<uint8, 3> second = {reply[3], reply[4], reply[5]};
	std::array<uint8, 3> third = {reply[6], reply[7], reply[8]};

	uint64 end = ScheduleChunk(line, simNowUs + 1000, first, CharacterUs);
	end = ScheduleChunk(line, end + 1600, second, CharacterUs);
	end = ScheduleChunk(line, end + 1600, third, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> received = link.Receive(buffer, 1000000);
	SIM_EXPECT(received.IsOk() && received.Value() == 9);
	bool same = true;
	for (std::size_t index = 0; index < reply.size(); index++) {
		same = same && buffer[index] == reply[index];
	}
	SIM_EXPECT(same);
	SIM_EXPECT(RtuFramer::Check(std::span<const uint8>(buffer.data(), 9), 0x01) == ResultStatus::ok);
}


// A gap between t1.5 and t3.5 inside a frame breaks it: invalidData. The retry on a clean line succeeds.
void TestGapBetweenT15AndT35Breaks() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	std::array<uint8, 4> first = {reply[0], reply[1], reply[2], reply[3]};
	std::array<uint8, 5> second = {reply[4], reply[5], reply[6], reply[7], reply[8]};

	uint64 end = ScheduleChunk(line, simNowUs + 1000, first, CharacterUs);
	end = ScheduleChunk(line, end + 2500, second, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> broken = link.Receive(buffer, 1000000);
	SIM_EXPECT(broken.IsErr() && broken.Error() == ResultStatus::invalidData);

	// The retry: the same reply after the silence that ends a frame.
	ScheduleChunk(line, end + InterFrameUs + 1000, reply, CharacterUs);
	Result<uint16> clean = link.Receive(buffer, 1000000);
	SIM_EXPECT(clean.IsOk() && clean.Value() == 9);
}


// A gap above t3.5 ends the frame. The bytes before the gap are a short frame; the rest is the next one.
void TestGapAboveT35Splits() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	std::array<uint8, 4> first = {reply[0], reply[1], reply[2], reply[3]};
	std::array<uint8, 5> second = {reply[4], reply[5], reply[6], reply[7], reply[8]};

	uint64 end = ScheduleChunk(line, simNowUs + 1000, first, CharacterUs);
	ScheduleChunk(line, end + 5000, second, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> shortFrame = link.Receive(buffer, 1000000);
	SIM_EXPECT(shortFrame.IsOk() && shortFrame.Value() == 4);

	// The short frame is refused by the framer (its CRC does not match), so a short reply never reaches the client as valid.
	SIM_EXPECT(RtuFramer::Check(std::span<const uint8>(buffer.data(), 4), 0x01) != ResultStatus::ok);

	Result<uint16> rest = link.Receive(buffer, 1000000);
	SIM_EXPECT(rest.IsOk() && rest.Value() == 5);
}


// Lenient accepts a gap above t1.5 inside a frame. A gap above t3.5 still ends it.
void TestLenientGaps() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	LenientLink link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::ok);

	std::array<uint8, 9> reply = NineByteReply();
	std::array<uint8, 4> first = {reply[0], reply[1], reply[2], reply[3]};
	std::array<uint8, 5> second = {reply[4], reply[5], reply[6], reply[7], reply[8]};

	// A 2 ms gap inside the frame is accepted.
	uint64 end = ScheduleChunk(line, simNowUs + 1000, first, CharacterUs);
	end = ScheduleChunk(line, end + 2000, second, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> whole = link.Receive(buffer, 1000000);
	SIM_EXPECT(whole.IsOk() && whole.Value() == 9);

	// A 4.5 ms gap is above t3.5 (4010 us at 9600), so it ends the frame.
	end = ScheduleChunk(line, end + 10000, first, CharacterUs);
	ScheduleChunk(line, end + 4500, second, CharacterUs);
	Result<uint16> split = link.Receive(buffer, 1000000);
	SIM_EXPECT(split.IsOk() && split.Value() == 4);
}


// A frame whose length is more than the buffer gives invalidData, and the ring is emptied so the next frame is clean.
void TestFrameTooLargeForBuffer() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	uint64 end = ScheduleChunk(line, simNowUs + 1000, reply, CharacterUs);

	std::array<uint8, 4> small{};
	Result<uint16> tooBig = link.Receive(small, 1000000);
	SIM_EXPECT(tooBig.IsErr() && tooBig.Error() == ResultStatus::invalidData);

	ScheduleChunk(line, end + InterFrameUs + 1000, reply, CharacterUs);
	std::array<uint8, 16> buffer{};
	Result<uint16> clean = link.Receive(buffer, 1000000);
	SIM_EXPECT(clean.IsOk() && clean.Value() == 9);
}


// A receive error event on a byte breaks the frame that byte belongs to. An error after the last byte breaks the frame too.
void TestReceiveErrorBreaksFrame() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	uint64 start = simNowUs + 1000;

	// The error is scheduled before the byte at the same time, as the port reports the error first.
	uint64 fifth = start + 4 * CharacterUs;
	line.ScheduleError(fifth, FakeLine::Error::Frame);
	uint64 end = ScheduleChunk(line, start, reply, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> broken = link.Receive(buffer, 1000000);
	SIM_EXPECT(broken.IsErr() && broken.Error() == ResultStatus::invalidData);

	// An overrun after the last byte: no byte follows, the frame still breaks.
	uint64 next = end + InterFrameUs + 1000;
	ScheduleChunk(line, next, reply, CharacterUs);
	line.ScheduleError(next + 9 * CharacterUs, FakeLine::Error::Overrun);
	Result<uint16> afterLast = link.Receive(buffer, 1000000);
	SIM_EXPECT(afterLast.IsErr() && afterLast.Error() == ResultStatus::invalidData);
}


// A full ring breaks the frame: 300 bytes in one frame overflow the 256-byte ring, and the frame gives invalidData.
void TestFullRingBreaksFrame() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 300> longFrame{};
	for (std::size_t index = 0; index < longFrame.size(); index++) {
		longFrame[index] = static_cast<uint8>(index);
	}

	ScheduleChunk(line, simNowUs + 1000, longFrame, CharacterUs);
	std::array<uint8, 256> buffer{};
	Result<uint16> overflowed = link.Receive(buffer, 2000000);
	SIM_EXPECT(overflowed.IsErr() && overflowed.Error() == ResultStatus::invalidData);
}


// Receive with timeout 0 returns at once with timeout, and a timed Receive returns timeout after its time.
void TestReceiveTimeouts() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 16> buffer{};
	uint64 before = simNowUs;
	Result<uint16> none = link.Receive(buffer, 0);
	SIM_EXPECT(none.IsErr() && none.Error() == ResultStatus::timeout);
	SIM_EXPECT(simNowUs - before < 100);

	before = simNowUs;
	Result<uint16> waited = link.Receive(buffer, 5000);
	SIM_EXPECT(waited.IsErr() && waited.Error() == ResultStatus::timeout);
	SIM_EXPECT(simNowUs - before >= 5000);
}


// With an RE pin the receiver is off from the start of the send to the end of the hold, so no echo arrives.
void TestEchoBlockedByRe() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	line.SetEcho(true);
	FakeGpio de;
	FakeGpio re;
	line.AttachPins(&de, &re);

	Link link(line, &de, &re);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	SIM_EXPECT(link.Send(reply, SendWaitUs) == ResultStatus::ok);
	SIM_EXPECT(line.Sent().size() == 9);

	std::array<uint8, 16> buffer{};
	Result<uint16> none = link.Receive(buffer, 0);
	SIM_EXPECT(none.IsErr() && none.Error() == ResultStatus::timeout);

	// RE rises before the first byte and falls after the hold.
	const std::vector<FakeGpio::Change>& changes = re.Changes();
	SIM_EXPECT(changes.size() == 2);
	SIM_EXPECT(changes.front().level && !changes.back().level);
	SIM_EXPECT(changes.front().timeUs <= line.Sent().front().endUs - CharacterUs);
	uint64 lastEnd = line.Sent().back().endUs;
	SIM_EXPECT(changes.back().timeUs - lastEnd >= TurnaroundUs);
	SIM_EXPECT(changes.back().timeUs - lastEnd <= TurnaroundUs + 10);
}


// Without an RE pin the echo arrives during the send. The clear after the hold drops it, so the next reply is clean.
void TestEchoDroppedWithoutRe() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	line.SetEcho(true);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> request = NineByteReply();
	SIM_EXPECT(link.Send(request, SendWaitUs) == ResultStatus::ok);
	uint64 lastEnd = line.Sent().back().endUs;

	std::array<uint8, 9> reply = NineByteReply();
	ScheduleChunk(line, lastEnd + InterFrameUs + 1000, reply, CharacterUs);

	std::array<uint8, 16> buffer{};
	Result<uint16> received = link.Receive(buffer, 1000000);
	SIM_EXPECT(received.IsOk() && received.Value() == 9);
	bool same = true;
	for (std::size_t index = 0; index < reply.size(); index++) {
		same = same && buffer[index] == reply[index];
	}
	SIM_EXPECT(same);
}


// DE is high before the first byte of the frame, and falls after the turnaround hold.
void TestDeTiming() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	FakeGpio de;
	line.AttachPins(&de, nullptr);
	Link link(line, &de);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 9> reply = NineByteReply();
	SIM_EXPECT(link.Send(reply, SendWaitUs) == ResultStatus::ok);

	bool allDriven = true;
	for (const FakeSentByte& sent : line.Sent()) {
		allDriven = allDriven && sent.driverOn;
	}
	SIM_EXPECT(allDriven);

	const std::vector<FakeGpio::Change>& changes = de.Changes();
	SIM_EXPECT(changes.size() == 2);
	SIM_EXPECT(changes.front().level && !changes.back().level);
	SIM_EXPECT(changes.front().timeUs <= line.Sent().front().endUs - CharacterUs);

	uint64 lastEnd = line.Sent().back().endUs;
	SIM_EXPECT(changes.back().timeUs - lastEnd >= TurnaroundUs);
	SIM_EXPECT(changes.back().timeUs - lastEnd <= TurnaroundUs + 10);
}


// The clock wraps every 2^32 microseconds. A gap across the wrap is measured correctly.
void TestGapAcrossClockWrap() {
	ResetSim((static_cast<uint64>(1) << 32) - 3000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 4> request = {1, 2, 3, 4};
	uint64 lastByte = ScheduleChunk(line, simNowUs + 1000, request, CharacterUs);

	std::array<uint8, 8> buffer{};
	Result<uint16> received = link.Receive(buffer, 1000000);
	SIM_EXPECT(received.IsOk() && received.Value() == 4);

	std::array<uint8, 4> reply = {5, 6, 7, 8};
	SIM_EXPECT(link.Send(reply, SendWaitUs) == ResultStatus::ok);
	SIM_EXPECT(line.Sent().front().endUs - CharacterUs >= lastByte + InterFrameUs);
}


void TestSendRejectsEmptyFrame() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 0> none{};
	SIM_EXPECT(link.Send(none, SendWaitUs) == ResultStatus::invalidParameter);
	SIM_EXPECT(line.Sent().empty());
}


// The write reports timeout after its bytes have left the line. The frame was sent, so Send returns writeError, not timeout: a caller
// that retries on timeout would otherwise put the same frame on the line twice. DE still drops after the hold.
void TestWriteTimeoutAfterBytesIsWriteError() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	FakeGpio de;
	line.AttachPins(&de, nullptr);
	line.SetWriteTimeout(true);
	Link link(line, &de);
	SIM_EXPECT(BeginLive(link));

	std::array<uint8, 4> frame = {1, 2, 3, 4};
	SIM_EXPECT(link.Send(frame, SendWaitUs) == ResultStatus::writeError);
	SIM_EXPECT(line.Sent().size() == 4);
	SIM_EXPECT(de.Changes().size() == 2 && !de.Changes().back().level);
}


// A line that never goes quiet: a byte every 2000 us, under t3.5, for five seconds. Send gives up after its timeout and sends nothing.
void TestSendGivesUpOnBusyLine() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	Link link(line);
	SIM_EXPECT(BeginLive(link));

	uint64 startUs = simNowUs;
	for (uint64 index = 0; index < 2500; index++) {
		line.ScheduleByte(startUs + index * 2000, 0x55);
	}

	std::array<uint8, 4> frame = {1, 2, 3, 4};
	SIM_EXPECT(link.Send(frame, 500000) == ResultStatus::timeout);
	SIM_EXPECT(line.Sent().empty());
	SIM_EXPECT(simNowUs - startUs < 510000);
}


// A receive ring entry is two bytes, so the default ring of 256 entries costs 512 bytes, not 2 KB.
void TestReceiveRingCost() {
	SIM_EXPECT(sizeof(Link) < 1024);
}


// Begin enables the receive interrupt before it installs its callbacks. When the adapter refuses the mode, the callbacks the user had
// installed stay in place.
void TestBeginKeepsCallbacksWhenRxModeFails() {
	ResetSim(1000000);
	FakeLine line;
	SetLine(line, 9600);
	int userBytes = 0;
	line.SetOnInterrupt([&userBytes](FakeLine::Irq type) {
		if (type == FakeLine::Irq::Rx) {
			userBytes++;
		}
	});

	line.SetRxModeFailure(true);
	Link link(line);
	SIM_EXPECT(link.Begin() == ResultStatus::error);

	line.ScheduleByte(simNowUs + 10, 0x42);
	simNowUs += 20;
	line.Pump();
	SIM_EXPECT(userBytes == 1);
}


int main() {
	TestBeginChecksClock();
	TestBeginNeedsBaudAndStart();
	TestSendWaitsForSilence();
	TestTimingIndependentOfFormat();
	TestReplyStartsAfterSilence();
	TestChunksBelowT15AreOneFrame();
	TestGapBetweenT15AndT35Breaks();
	TestGapAboveT35Splits();
	TestLenientGaps();
	TestFrameTooLargeForBuffer();
	TestReceiveErrorBreaksFrame();
	TestFullRingBreaksFrame();
	TestReceiveTimeouts();
	TestEchoBlockedByRe();
	TestEchoDroppedWithoutRe();
	TestDeTiming();
	TestGapAcrossClockWrap();
	TestSendRejectsEmptyFrame();
	TestSendGivesUpOnBusyLine();
	TestWriteTimeoutAfterBytesIsWriteError();
	TestReceiveRingCost();
	TestBeginKeepsCallbacksWhenRxModeFails();
	return SimFinish("TestRtuTransport");
}
