#include <VHAL.h>
#include "NcmTestDevices.h"
#include "SimAssert.h"
#include "SimHost.h"
#include <array>
#include <cstddef>
#include <span>


// The session boundary of the data path. A block the host sent in one session may still be waiting for the parse when the session
// ends, because the host closes the path, resets the bus or deconfigures before the application calls Process(). That block belongs to
// the closed session: it must not be delivered into the next one, and the OUT endpoint must be armed again in the new session. The bytes
// are written by hand from NCM 1.0 section 3, as in TestNcmData.cpp.
namespace {


// Frame A: a 14-byte header only, to ff:ff:ff:ff:ff:ff from 02:00:00:00:00:01, type IPv4 (08 00).
constexpr uint8 FrameA[14] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00
};


// Frame B: 20 bytes, a header and six payload bytes 10 to 15.
constexpr uint8 FrameB[20] = {
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x08, 0x00,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15
};


// Frame A alone, sequence 0: block length 44 (0x2C), NDP at 28 (0x1C) with (12, 14).
constexpr uint8 FrameABlock[44] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x2C, 0x00, 0x1C, 0x00,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00,
	0x00, 0x00,
	0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00
};


// Frame B alone, sequence 0: 12 + 20 = 32, the NDP at 32 (0x20) with (12, 20 = 0x14). Block length 48 (0x30).
constexpr uint8 FrameBBlockSeq0[48] = {
	0x4E, 0x43, 0x4D, 0x48, 0x0C, 0x00, 0x00, 0x00, 0x30, 0x00, 0x20, 0x00,
	0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
	0x4E, 0x43, 0x4D, 0x30, 0x10, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00
};


static_assert(sizeof(FrameBBlockSeq0) == 48, "the block of frame B at sequence 0 is 48 bytes");


// Counts the blocks the application is told about through the frame-received delegate.
struct FrameEvents {
	uint32 blocks = 0;
};


void CountFrameBlock(void* context) {
	static_cast<FrameEvents*>(context)->blocks++;
}


// Takes the armed IN transfer of the bulk IN endpoint and checks it against the expected block, byte for byte.
template<typename TFixture, std::size_t Size>
void ExpectInBlock(TFixture& fixture, const uint8 (&expected)[Size]) {
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	std::array<uint8, 2048> in = {};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, Size);
	if (received == Size) {
		for (std::size_t index = 0; index < Size; index++) {
			SIM_EXPECT_EQ(in[index], expected[index]);
		}
	}
}


// The host sends one block on the bulk OUT endpoint, then the device parses it at its next Process().
template<typename TFixture, std::size_t Size>
void HostSendBlock(TFixture& fixture, const uint8 (&block)[Size]) {
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT(fixture.sim.HostOut(0x02, block).IsOk());
}


// The receive path with the frame events counted and the data path open. The block the host sends waits in the OUT buffer until Process().
void OpenSession(NcmFixture& fixture, FrameEvents& events) {
	SIM_EXPECT_EQ(fixture.function.SetFrameReceivedHandler(NcmTest::Function::FrameReceivedDelegate::FromRaw(&CountFrameBlock, &events)), ResultStatus::ok);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
}


// The expectations after a block that belonged to a closed session was offered to the new one: nothing is delivered, the block is counted
// as dropped stale and not as received, and the OUT endpoint is armed again. A block the host sends now is delivered as usual.
void ExpectStaleBlockDropped(NcmFixture& fixture, FrameEvents& events) {
	fixture.device.Process();
	SIM_EXPECT_EQ(events.blocks, 0);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 0);
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.ntbsReceived, 0);
	SIM_EXPECT_EQ(counters.ntbsDroppedStale, 1);
	SIM_EXPECT_EQ(counters.framesReceived, 0);
	SIM_EXPECT_EQ(counters.framesDroppedReceive, 0);
	SIM_EXPECT_EQ(counters.ntbsMalformed, 0);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));

	HostSendBlock(fixture, FrameABlock);
	fixture.device.Process();
	SIM_EXPECT_EQ(events.blocks, 1);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 14);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// alternate 1, then alternate 0 and 1 again before Process(): the block of the first session is dropped.
void TestStaleBlockAfterReopen() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, FrameABlock);
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectStaleBlockDropped(fixture, events);
}


// The same with a bus reset in between: the bus reset ends the session, and the host selects alternate 1 again after enumeration.
void TestStaleBlockAfterBusReset() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, FrameABlock);
	fixture.sim.InjectBusReset();
	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectStaleBlockDropped(fixture, events);
}


// The same with a deconfiguration and a new configuration in between.
void TestStaleBlockAfterDeconfigure() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, FrameABlock);
	SIM_EXPECT_EQ(fixture.Deconfigure(), ResultStatus::ok);
	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	ExpectStaleBlockDropped(fixture, events);
}


// A block that the host sent in the session and that Process() parses in the same session is delivered: the rule is about the session, not
// about the block being parsed late. The OUT endpoint is armed after the parse, so the next block is accepted.
void TestBlockInSessionDelivered() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, FrameABlock);
	fixture.device.Process();
	SIM_EXPECT_EQ(events.blocks, 1);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 14);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// alternate 1 with a frame in flight and a frame queued behind it, then alternate 0 and 1 before Process(). Both frames belong to the first
// session and are dropped; the sequence of the next session starts at 0.
void TestReopenDropsQueueAndInFlightBlock() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameBBlockSeq0);

	// The NTB of session A was in flight when the path closed: the endpoint abandoned it, so its frame is dropped, not sent. The frame queued
	// behind it went with the session too. Only the frame of session B is sent.
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 1);
	SIM_EXPECT_EQ(counters.ntbsSent, 1);
	SIM_EXPECT_EQ(counters.framesDroppedSend, 2);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The OUT endpoint halted with a received block waiting: the block is parsed and delivered, and the endpoint is not armed while it is
// halted. The clear arms it again.
void TestOutHaltBlocksArmUntilClear() {
	NcmFixture fixture;
	FrameEvents events;
	OpenSession(fixture, events);

	HostSendBlock(fixture, FrameABlock);
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0002, 0)), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(events.blocks, 1);
	SIM_EXPECT_EQ(fixture.function.NextFrameLength(), 14);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x02));

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0002, 0)), ResultStatus::ok);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The adapter of the session-change test. Its hook runs once, right after the next IN transfer is armed, so a session change lands inside the
// Process() call that started the transfer, as an interrupt could make it.
struct ToggleAdapter : public SimUSBDeviceAdapter {
	bool hookArmed = false;
	void (*hook)(void*) = nullptr;
	void* hookContext = nullptr;


	ResultStatus StartInTransfer(uint8 address, std::span<const uint8> data, bool sendZeroLengthPacket) override {
		ResultStatus status = SimUSBDeviceAdapter::StartInTransfer(address, data, sendZeroLengthPacket);
		if (hookArmed && status == ResultStatus::ok) {
			hookArmed = false;
			hook(hookContext);
		}

		return status;
	}
};


// The adapter of the OUT-arm test. It refuses the next refusedOuts arms with busy, as an endpoint that is still busy does.
struct RefusingAdapter : public SimUSBDeviceAdapter {
	uint32 refusedOuts = 0;


	ResultStatus StartOutTransfer(uint8 address, std::span<uint8> buffer) override {
		if (refusedOuts > 0) {
			refusedOuts--;
			return ResultStatus::busy;
		}

		return SimUSBDeviceAdapter::StartOutTransfer(address, buffer);
	}
};


using ToggleFixture = NcmTest::NcmFixtureOf<NcmTest::ExplicitUsb, NcmTest::WideFunction, ToggleAdapter>;
using RefusingFixture = NcmTest::NcmFixtureOf<NcmTest::ExplicitUsb, NcmTest::Function, RefusingAdapter>;


// Halts or clears the halt of an endpoint, as the host does with CLEAR_FEATURE(ENDPOINT_HALT).
template<typename TFixture>
ResultStatus HaltEndpoint(TFixture& fixture, uint8 address, bool halt) {
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, halt ? 3 : 1, 0x0000, address, 0));
}


// The 4-byte SET_NTB_INPUT_SIZE request: dwNtbInMaxSize, little-endian.
template<typename TFixture>
ResultStatus SetNtbInputSize(TFixture& fixture, uint32 size) {
	const uint8 data[4] = {
		static_cast<uint8>(size & 0xFF),
		static_cast<uint8>((size >> 8) & 0xFF),
		static_cast<uint8>((size >> 16) & 0xFF),
		static_cast<uint8>((size >> 24) & 0xFF)
	};
	uint32 received = 0;
	return SimHost::Control(fixture.sim, SimHost::MakeSetup(0x21, 0x86, 0x0000, 0x0000, 4), std::span<const uint8>(data), std::span<uint8>(), received);
}


void ToggleAlternate(void* context) {
	ToggleFixture& fixture = *static_cast<ToggleFixture*>(context);
	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
}


// Frames queued in session A, then a session change inside the Process() call that starts an IN transfer. No reset was applied before that
// call read the queue, so the frame is still queued when session B begins. The reset is applied by the next call, which drops the frame and
// counts it; nothing of session A is sent in session B.
void TestSessionChangeInsideProcessDropsQueuedFrame() {
	ToggleFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);

	std::array<uint8, 1514> frameOne = {};
	std::array<uint8, 1514> frameTwo = {};
	std::array<uint8, 20> frameStale = {};
	frameOne.fill(0x11);
	frameTwo.fill(0x22);
	frameStale.fill(0x33);

	// Two big frames make a 3064-byte NTB. It goes in flight, the halt abandons it, and it waits as a ready block until the input size fits it.
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameOne), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameTwo), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(HaltEndpoint(fixture, 0x81, true), ResultStatus::ok);
	SIM_EXPECT_EQ(SetNtbInputSize(fixture, 2048), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(frameStale), ResultStatus::ok);
	SIM_EXPECT_EQ(HaltEndpoint(fixture, 0x81, false), ResultStatus::ok);
	SIM_EXPECT_EQ(SetNtbInputSize(fixture, 4096), ResultStatus::ok);

	// The ready block fits again, so the next Process() starts it, and the session change lands inside that call.
	fixture.sim.hookArmed = true;
	fixture.sim.hook = &ToggleAlternate;
	fixture.sim.hookContext = &fixture;
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));

	// The reset is applied first by this call: the stale frame goes, and nothing is armed for session B yet.
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	NcmTest::WideFunction::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 0);
	SIM_EXPECT_EQ(counters.ntbsSent, 0);
	// The two frames of the abandoned block and the stale frame.
	SIM_EXPECT_EQ(counters.framesDroppedSend, 3);

	// A frame queued in session B goes out alone, from sequence 0.
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameB), ResultStatus::ok);
	fixture.device.Process();
	ExpectInBlock(fixture, FrameBBlockSeq0);
	counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 1);
	SIM_EXPECT_EQ(counters.ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// An NTB in flight when the data path closes is abandoned by the endpoint with no completion. Its frame never reaches the host, so it moves
// from the sent counts to the dropped count at the close.
void TestInFlightNtbAbandonedByCloseIsDropped() {
	NcmFixture fixture;
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	SIM_EXPECT_EQ(fixture.SelectAlternate(0), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesSent, 0);
	SIM_EXPECT_EQ(counters.ntbsSent, 0);
	SIM_EXPECT_EQ(counters.framesDroppedSend, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// A session end clears the queued frames of both directions, and each one is counted: the frames queued for sending, and the received frame
// the application did not read.
void TestSessionEndCountsClearedFrames() {
	NcmFixture sender;
	sender.Configure();
	SIM_EXPECT_EQ(sender.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(sender.function.SendFrame(FrameA), ResultStatus::ok);
	SIM_EXPECT_EQ(sender.function.SendFrame(FrameB), ResultStatus::ok);
	SIM_EXPECT_EQ(sender.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(sender.SelectAlternate(1), ResultStatus::ok);
	sender.device.Process();
	NcmTest::Function::Counters counters = sender.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesDroppedSend, 2);
	SIM_EXPECT_EQ(counters.framesSent, 0);
	SIM_EXPECT_EQ(sender.device.GetErrorCount(), 0);

	FrameEvents events;
	NcmFixture receiver;
	OpenSession(receiver, events);
	HostSendBlock(receiver, FrameABlock);
	receiver.device.Process();
	SIM_EXPECT_EQ(receiver.function.GetCounters().framesReceived, 1);
	SIM_EXPECT_EQ(receiver.SelectAlternate(0), ResultStatus::ok);
	SIM_EXPECT_EQ(receiver.SelectAlternate(1), ResultStatus::ok);
	receiver.device.Process();
	counters = receiver.function.GetCounters();
	SIM_EXPECT_EQ(counters.framesDroppedReceive, 1);
	SIM_EXPECT_EQ(receiver.function.NextFrameLength(), 0);
	SIM_EXPECT_EQ(receiver.device.GetErrorCount(), 0);
}


// The OUT arm that the endpoint refuses leaves the endpoint unarmed while no block is held. Process() arms it again, so the host's next
// block is taken.
void TestRefusedOutArmRetriedByProcess() {
	RefusingFixture fixture;
	fixture.Configure();
	fixture.sim.refusedOuts = 1;
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x02));

	fixture.device.Process();
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x02));
	HostSendBlock(fixture, FrameABlock);
	fixture.device.Process();
	NcmTest::Function::Counters counters = fixture.function.GetCounters();
	SIM_EXPECT_EQ(counters.ntbsReceived, 1);
	SIM_EXPECT_EQ(counters.framesReceived, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// What the data path's handler finds when it calls the queue API from interrupt context, and what the call returns.
struct InterruptCalls {
	NcmTest::Function* function = nullptr;
	ResultStatus send = ResultStatus::ok;
	uint16 length = 99;
	uint16 received = 99;
	uint32 calls = 0;
};


// Runs as the started handler does: in interrupt context. The test marks the context for the calls, as the interrupt would be.
void CallQueueApiFromInterrupt(void* context) {
	InterruptCalls& calls = *static_cast<InterruptCalls*>(context);
	std::array<uint8, 1514> buffer = {};
	simInterruptContext = true;
	calls.send = calls.function->SendFrame(FrameA);
	calls.length = calls.function->NextFrameLength();
	calls.received = calls.function->ReceiveFrame(buffer);
	simInterruptContext = false;
	calls.calls++;
}


// From interrupt context the queue API refuses: SendFrame is busy, NextFrameLength and ReceiveFrame return 0. The refused frame is not queued,
// so the frame the thread sends is the only one in the block.
void TestQueueApiRefusedFromInterrupt() {
	NcmFixture fixture;
	InterruptCalls calls;
	calls.function = &fixture.function;
	SIM_EXPECT_EQ(fixture.function.SetDataPathStartedHandler(NcmTest::Function::DataPathDelegate::FromRaw(&CallQueueApiFromInterrupt, &calls)), ResultStatus::ok);
	fixture.Configure();
	SIM_EXPECT_EQ(fixture.SelectAlternate(1), ResultStatus::ok);
	SIM_EXPECT_EQ(calls.calls, 1);
	SIM_EXPECT_EQ(calls.send, ResultStatus::busy);
	SIM_EXPECT_EQ(calls.length, 0);
	SIM_EXPECT_EQ(calls.received, 0);

	SIM_EXPECT_EQ(fixture.function.SendFrame(FrameA), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.function.GetCounters().framesSent, 1);
	SIM_EXPECT_EQ(fixture.function.GetCounters().ntbsSent, 1);
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


}


int main() {
	TestStaleBlockAfterReopen();
	TestStaleBlockAfterBusReset();
	TestStaleBlockAfterDeconfigure();
	TestBlockInSessionDelivered();
	TestReopenDropsQueueAndInFlightBlock();
	TestOutHaltBlocksArmUntilClear();
	TestSessionChangeInsideProcessDropsQueuedFrame();
	TestInFlightNtbAbandonedByCloseIsDropped();
	TestSessionEndCountsClearedFrames();
	TestRefusedOutArmRetriedByProcess();
	TestQueueApiRefusedFromInterrupt();
	return SimTest::Finish("TestNcmSession");
}
