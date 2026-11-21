#include <VHAL.h>
#include "Descriptors.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Class/MIDI/MidiFunction.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <span>

// The USB-MIDI function run through the simulated controller with exact bytes. Every expected packet is written by hand from
// the USB-MIDI event packet layout (MIDI 1.0 section 4): byte 0 is cable << 4 | code index, bytes 1 to 3 the MIDI bytes. The
// queues are small, so their limits are reached with few events. The cases are the channel helpers, the argument checks,
// SysEx splitting for lengths 2 to 7 and a long message, the SysEx framing and data rules, the queue limits, the receive path with
// its SysEx stream, the OUT back-pressure, the bus reset, the endpoint halt, and a start that fails after a completion ran inside it.


namespace {


constexpr uint8 Interfaces[] = {
	0
};


constexpr uint8 Endpoints[] = {
	0x81,
	0x01
};


// Two cables, 32 events in each direction.
using Midi = MidiFunction<2, 32, 32>;


// Records what the handlers receive, so the cases can compare it. The handlers run in interrupt context in the device, so they
// only store into fixed arrays.
struct Recorder {
	std::array<USBMidiPacket, 8> events{};
	std::size_t eventCount = 0;
	std::array<uint8, 16> sysEx{};
	std::size_t sysExCount = 0;
	uint8 sysExCable = 0xFF;


	void OnEvent(const USBMidiPacket& packet) {
		if (eventCount < events.size()) {
			events[eventCount] = packet;
			eventCount++;
		}
	}


	void OnSysEx(uint8 cable, std::span<const uint8> bytes) {
		sysExCable = cable;
		for (uint8 value : bytes) {
			if (sysExCount < sysEx.size()) {
				sysEx[sysExCount] = value;
				sysExCount++;
			}
		}
	}
};


struct MidiFixture {
	SimUSBDeviceAdapter sim;
	Midi midi;
	Recorder recorder;
	std::array<USBFunction*, 1> functions;
	USBDevice device;


	MidiFixture()
		: midi(Interfaces, Endpoints),
		  functions{ &midi },
		  device(sim, SimDescriptors::SimpleSet, functions) {
		SIM_EXPECT_EQ(midi.SetEventHandler(Midi::EventHandler::Bind<Recorder, &Recorder::OnEvent>(&recorder)), ResultStatus::ok);
		SIM_EXPECT_EQ(midi.SetSysExHandler(Midi::SysExHandler::Bind<Recorder, &Recorder::OnSysEx>(&recorder)), ResultStatus::ok);
	}


	// Attaches the device and configures it, as a host does.
	void Configure() {
		SIM_EXPECT_EQ(device.Start(USBDeviceAdapter::Parameters()), ResultStatus::ok);
		Enumerate();
	}


	// Resets the bus and configures the device again.
	void Enumerate() {
		sim.InjectBusReset();
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
		SIM_EXPECT_EQ(device.GetState(), USBDeviceState::Configured);
	}


	// Every case ends with no critical section open, so a function that leaves one open fails the case.
	~MidiFixture() {
		SimAssertNoCriticalSection();
	}


	// Reads the IN endpoint until the host gets no more. Process() runs before each read, as the application loop does, so a
	// transfer the completion did not start is started here.
	std::size_t ReadIn(std::span<uint8> out) {
		std::size_t total = 0;
		for (int step = 0; step < 64; step++) {
			device.Process();
			uint32 received = 0;
			if (sim.HostIn(0x81, out.subspan(total), received) != ResultStatus::ok) {
				break;
			}

			total += received;
		}

		return total;
	}


	// Sends the host's OUT packets as one data stage on endpoint 0x01.
	ResultStatus HostSend(std::span<const uint8> data) {
		Result<uint32> stored = sim.HostOut(0x01, data);
		if (!stored.IsOk()) {
			return stored.Error();
		}

		return ResultStatus::ok;
	}
};


}


// The channel helpers, each checked against its hand-written packet. Cable 1 is used where a case checks the cable nibble.
void TestChannelHelpers() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(1, 9, 60, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.NoteOff(0, 0, 60, 0), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.PolyKeyPressure(0, 1, 64, 50), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.ControlChange(1, 3, 7, 127), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.ProgramChange(0, 5, 42), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.ChannelPressure(0, 2, 99), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.PitchBend(0, 0, 8192), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.PitchBend(0, 0, 16383), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x09, 0x90, 0x3C, 0x64,
		0x19, 0x99, 0x3C, 0x64,
		0x08, 0x80, 0x3C, 0x00,
		0x0A, 0xA1, 0x40, 0x32,
		0x1B, 0xB3, 0x07, 0x7F,
		0x0C, 0xC5, 0x2A, 0x00,
		0x0D, 0xD2, 0x63, 0x00,
		0x0E, 0xE0, 0x00, 0x40,
		0x0E, 0xE0, 0x7F, 0x7F
	};

	std::array<uint8, 256> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// Argument checks: a cable outside the function, a channel or data byte out of range, a reserved code index and a pitch bend
// above 14 bits are refused, and none of them queues a packet.
void TestArgumentChecks() {
	MidiFixture fixture;

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 100), ResultStatus::notReady);

	fixture.Configure();
	SIM_EXPECT_EQ(fixture.midi.NoteOn(2, 0, 60, 100), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 16, 60, 100), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 128, 100), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 128), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.PitchBend(0, 0, 16384), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x1, 0, 0, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x0, 0, 0, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);
}


// SysEx split for every valid length from 2 to 7, with cable 1. A message is framed by F0 and F7, so the shortest is {F0, F7}. Every
// packet before the last one is CIN 0x4 with three bytes. The last one closes the message: CIN 0x7 when the bytes left are three, 0x6
// for two and 0x5 for one. The 20-byte message is in TestSysExLongMessage.
void TestSysExSplit() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 message1[] = { 0xF0, 0xF7 };
	constexpr uint8 message2[] = { 0xF0, 0x01, 0xF7 };
	constexpr uint8 message3[] = { 0xF0, 0x01, 0x02, 0xF7 };
	constexpr uint8 message4[] = { 0xF0, 0x01, 0x02, 0x03, 0xF7 };
	constexpr uint8 message5[] = { 0xF0, 0x01, 0x02, 0x03, 0x04, 0xF7 };
	constexpr uint8 message6[] = { 0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0xF7 };

	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message1), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message2), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message3), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message4), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message5), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(1, message6), ResultStatus::ok);

	// Eleven packets in all: one each for lengths 2 and 3, two each for 4 to 6, three for 7. They fit one transfer.
	constexpr uint8 expected[] = {
		0x16, 0xF0, 0xF7, 0x00,
		0x17, 0xF0, 0x01, 0xF7,
		0x14, 0xF0, 0x01, 0x02,
		0x15, 0xF7, 0x00, 0x00,
		0x14, 0xF0, 0x01, 0x02,
		0x16, 0x03, 0xF7, 0x00,
		0x14, 0xF0, 0x01, 0x02,
		0x17, 0x03, 0x04, 0xF7,
		0x14, 0xF0, 0x01, 0x02,
		0x14, 0x03, 0x04, 0x05,
		0x15, 0xF7, 0x00, 0x00
	};

	std::array<uint8, 512> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// The 20-byte message on its own, to check the closing packet of a long message and the transfer boundary.
void TestSysExLongMessage() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 message[] = {
		0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09,
		0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0xF7
	};

	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, message), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 7);

	constexpr uint8 expected[] = {
		0x04, 0xF0, 0x01, 0x02,
		0x04, 0x03, 0x04, 0x05,
		0x04, 0x06, 0x07, 0x08,
		0x04, 0x09, 0x0A, 0x0B,
		0x04, 0x0C, 0x0D, 0x0E,
		0x04, 0x0F, 0x10, 0x11,
		0x06, 0x12, 0xF7, 0x00
	};

	std::array<uint8, 512> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// The queue holds 32 events. A full queue refuses the next event with busy and a SysEx message that does not fit whole, and a
// message longer than the queue is refused as invalid. Nothing is queued by a refusal.
void TestQueueLimits() {
	MidiFixture fixture;
	fixture.Configure();

	for (uint8 note = 0; note < 32; note++) {
		SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, note, 1), ResultStatus::ok);
	}

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 40, 1), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 32);

	// The queue is full, so even a one-packet SysEx is busy and nothing changes.
	constexpr uint8 small[] = { 0xF0, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, small), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 32);

	// A message longer than the queue can never fit: 33 packets (97 to 99 bytes) against 32 events. It is framed, so the length is the
	// only reason it is refused.
	std::array<uint8, 97> tooLong{};
	tooLong[0] = 0xF0;
	tooLong.back() = 0xF7;
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, tooLong), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, std::span<const uint8>()), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(2, small), ResultStatus::invalidParameter);

	// Reading frees the queue: after a full read the queue takes events again.
	std::array<uint8, 512> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, 32 * 4);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 40, 1), ResultStatus::ok);
}


// A message is queued whole or not at all: with two free slots a three-packet message is busy and a two-packet one is queued.
void TestSysExWholeOrNothing() {
	MidiFixture fixture;
	fixture.Configure();

	for (uint8 note = 0; note < 30; note++) {
		SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, note, 1), ResultStatus::ok);
	}

	// Seven bytes need three packets, so they do not fit in the two free slots.
	constexpr uint8 threePackets[] = { 0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, threePackets), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 30);

	// Four bytes need two packets, and they fit.
	constexpr uint8 twoPackets[] = { 0xF0, 0x01, 0x02, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, twoPackets), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 32);
}


// Received packets reach the RX queue and the event handler in order. A reserved code index is skipped, and the SysEx packets
// give their bytes to the SysEx handler as a stream, with the cable of the packet.
void TestReceivePath() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 received[] = {
		0x09, 0x90, 0x3C, 0x64,
		0x00, 0x00, 0x00, 0x00,
		0x14, 0xF0, 0x01, 0x02,
		0x15, 0xF7, 0x00, 0x00
	};

	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();

	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 3);

	USBMidiPacket packet;
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.bytes[0], 0x09);
	SIM_EXPECT_EQ(packet.bytes[1], 0x90);
	SIM_EXPECT_EQ(packet.bytes[2], 0x3C);
	SIM_EXPECT_EQ(packet.bytes[3], 0x64);

	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.bytes[0], 0x14);
	SIM_EXPECT_EQ(packet.bytes[1], 0xF0);

	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.bytes[0], 0x15);
	SIM_EXPECT_EQ(packet.bytes[1], 0xF7);

	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::notReady);

	SIM_EXPECT_EQ(fixture.recorder.eventCount, 3);
	SIM_EXPECT_EQ(fixture.recorder.sysExCount, 4);
	SIM_EXPECT_EQ(fixture.recorder.sysExCable, 1);
	SIM_EXPECT_EQ(fixture.recorder.sysEx[0], 0xF0);
	SIM_EXPECT_EQ(fixture.recorder.sysEx[1], 0x01);
	SIM_EXPECT_EQ(fixture.recorder.sysEx[2], 0x02);
	SIM_EXPECT_EQ(fixture.recorder.sysEx[3], 0xF7);
}


// The OUT endpoint is armed only while the RX queue has room for a whole transfer. Two transfers of 16 packets fill the queue; the
// endpoint then NAKs until the thread reads, and the next Process() arms it again.
void TestReceiveBackPressure() {
	MidiFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> transfer{};
	for (std::size_t index = 0; index < transfer.size(); index += 4) {
		transfer[index] = 0x09;
		transfer[index + 1] = 0x90;
		transfer[index + 2] = static_cast<uint8>(index / 4);
		transfer[index + 3] = 0x40;
	}

	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 16);

	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 32);

	// The queue is full: the endpoint is not armed, so the host's transfer is not taken.
	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::notReady);

	USBMidiPacket packet;
	for (int index = 0; index < 16; index++) {
		SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	}

	// Room for one transfer again: Process() arms the endpoint and the host's transfer is taken.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 32);
}


// A bus reset and a configuration clear both queues: events queued before the reset are not sent after it, and a received event
// is not read after it.
void TestResetClearsQueues() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 100), ResultStatus::ok);
	constexpr uint8 received[] = { 0x09, 0x90, 0x3C, 0x64 };
	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 1);

	// A bus reset makes the received event unreadable before the host configures the device again.
	USBMidiPacket early;
	fixture.sim.InjectBusReset();
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(early), ResultStatus::notReady);
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 0);

	fixture.Enumerate();
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 0);

	USBMidiPacket packet;
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::notReady);

	std::array<uint8, 64> in{};
	SIM_EXPECT_EQ(fixture.ReadIn(in), 0);
}


// A halt of the IN endpoint abandons the transfer in flight, and its packets go back to the queue. After ClearStall the host
// reads the same packets, in order, once.
void TestHaltReturnsInFlightPackets() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 62, 100), ResultStatus::ok);
	fixture.device.Process();

	// The host halts the IN endpoint before it reads the transfer.
	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0081, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 2);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0081, 0)), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x09, 0x90, 0x3C, 0x64,
		0x09, 0x90, 0x3E, 0x64
	};

	std::array<uint8, 64> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// A stalled OUT endpoint stops arming, and it arms again after ClearStall.
void TestOutHaltStopsArming() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 3, 0x0000, 0x0001, 0)), ResultStatus::ok);

	constexpr uint8 received[] = { 0x09, 0x90, 0x3C, 0x64 };
	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::accessError);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 0);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x02, 1, 0x0000, 0x0001, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 1);
}


// Raw events go through the same checks as the helpers. Malformed packets are refused and queue nothing; a tune request and a
// real-time clock, which carry no data bytes, are queued as they are.
void TestSendEventRejectsMalformed() {
	MidiFixture fixture;
	fixture.Configure();

	// A data byte where the status goes, a data byte of 8 bits, a status that disagrees with the code index, a nonzero unused byte,
	// SysEx framing, a single-byte code index with a System Common byte other than 0xF6.
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x9, 0x20, 0x40, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x9, 0x90, 0x80, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x9, 0x80, 0x40, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0xC, 0xC0, 0x05, 0x01)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x4, 0xF0, 0x01, 0x02)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x7, 0x03, 0xF7, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x5, 0xF1, 0, 0)), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);

	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x5, 0xF6, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0xF, 0xF8, 0, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.SendEvent(USBMidiPacket::Make(0, 0x2, 0xF3, 0x05, 0)), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x05, 0xF6, 0x00, 0x00,
		0x0F, 0xF8, 0x00, 0x00,
		0x02, 0xF3, 0x05, 0x00
	};

	std::array<uint8, 64> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// A 0x5 packet with 0xF6 is a System Common message, not SysEx data, so the SysEx handler does not see it. The packet still reaches
// the RX queue and the event handler.
void TestReceiveSystemCommonNotSysEx() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 received[] = { 0x15, 0xF6, 0x00, 0x00 };
	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();

	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 1);
	SIM_EXPECT_EQ(fixture.recorder.eventCount, 1);
	SIM_EXPECT_EQ(fixture.recorder.sysExCount, 0);
}


// The OUT endpoint needs room for a whole transfer: with 15 free slots the host is NAKed, even after Process(), and with 16 it is taken.
void TestReceiveBackPressureThreshold() {
	MidiFixture fixture;
	fixture.Configure();

	std::array<uint8, 64> transfer{};
	for (std::size_t index = 0; index < transfer.size(); index += 4) {
		transfer[index] = 0x09;
		transfer[index + 1] = 0x90;
		transfer[index + 2] = static_cast<uint8>(index / 4);
		transfer[index + 3] = 0x40;
	}

	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 32);

	USBMidiPacket packet;
	for (int index = 0; index < 15; index++) {
		SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	}

	// Fifteen free slots: one packet short of a transfer, so the host is NAKed and nothing is stored.
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::notReady);
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 17);

	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.HostSend(transfer), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 32);
}


// The completion of one IN transfer starts the next, so the host reads the second transfer without Process() in between.
void TestNextInTransferStartsFromCompletion() {
	MidiFixture fixture;
	fixture.Configure();

	for (uint8 note = 0; note < 20; note++) {
		SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, note, 1), ResultStatus::ok);
	}

	fixture.device.Process();

	std::array<uint8, 64> first{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, first, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT_EQ(first[2], 0);
	SIM_EXPECT_EQ(first[62], 15);

	// No Process() here: the second transfer must already be armed by the first one's completion.
	std::array<uint8, 64> second{};
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, second, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 16);
	SIM_EXPECT_EQ(second[2], 16);
	SIM_EXPECT_EQ(second[14], 19);

	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, second, received), ResultStatus::notReady);
}


// SET_CONFIGURATION(0) deconfigures the function. The received events it leaves behind are not readable until the next
// configuration clears them.
void TestDeconfigureHidesReceivedEvents() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 received[] = { 0x09, 0x90, 0x3C, 0x64 };
	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 1);

	SIM_EXPECT_EQ(SimHost::Control(fixture.sim, SimHost::MakeSetup(0x00, 9, 0, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 0);

	USBMidiPacket packet;
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::notReady);
}


// The handlers cannot change while the function is configured, because the device calls them from its context.
void TestHandlersBusyWhileConfigured() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.SetEventHandler(Midi::EventHandler::Bind<Recorder, &Recorder::OnEvent>(&fixture.recorder)), ResultStatus::busy);
	SIM_EXPECT_EQ(fixture.midi.SetSysExHandler(Midi::SysExHandler::Bind<Recorder, &Recorder::OnSysEx>(&fixture.recorder)), ResultStatus::busy);
}


// The adapter refuses the IN start, because it is not connected, so the packets marked for that transfer go back to the queue. After
// the adapter connects again, the host reads them in order. Without the give-back the marks stay set, and every later build fails
// to mark, so the host is NAKed for good.
void TestRefusedInStartKeepsPackets() {
	MidiFixture fixture;
	fixture.Configure();

	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 60, 100), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, 62, 100), ResultStatus::ok);

	SIM_EXPECT_EQ(fixture.sim.Disconnect(), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 2);

	SIM_EXPECT_EQ(fixture.sim.Connect(), ResultStatus::ok);

	constexpr uint8 expected[] = {
		0x09, 0x90, 0x3C, 0x64,
		0x09, 0x90, 0x3E, 0x64
	};

	std::array<uint8, 64> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, sizeof(expected));
	SIM_EXPECT(SimHost::SameBytes(std::span<const uint8>(in).first(count), std::span<const uint8>(expected)));
}


// A status byte inside a SysEx message ends it on the host, so the bytes after the first must be 7-bit. A final F7 is the one byte
// allowed to have bit 7 set. A refused message queues nothing.
void TestSysExDataBytesAre7Bit() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 statusInside[] = { 0xF0, 0x90, 0x40, 0x7F, 0xF7 };
	constexpr uint8 endInside[] = { 0xF0, 0x01, 0xF7, 0x02, 0xF7 };
	constexpr uint8 startInside[] = { 0xF0, 0x01, 0xF0, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, statusInside), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, endInside), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, startInside), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);

	constexpr uint8 closed[] = { 0xF0, 0x01, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, closed), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 1);
}


// A SysEx message is framed by F0 at its start and F7 at its end. A message without either, a single F0 or F7, and one that starts with
// a data byte are refused, and a refused message queues nothing. A framed message on the same cable still goes out.
void TestSysExFramingRejected() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 onlyStart[] = { 0xF0 };
	constexpr uint8 onlyEnd[] = { 0xF7 };
	constexpr uint8 noStart[] = { 0x01, 0xF7 };
	constexpr uint8 noEnd[] = { 0xF0, 0x01 };
	constexpr uint8 noEndThreeBytes[] = { 0xF0, 0x01, 0x02 };
	constexpr uint8 dataFirst[] = { 0x01, 0x02, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, onlyStart), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, onlyEnd), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, noStart), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, noEnd), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, noEndThreeBytes), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, dataFirst), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);

	constexpr uint8 framed[] = { 0xF0, 0xF7 };
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, framed), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 1);
}


// A context for the function's own calls, without a device. Its start calls can run the completion of their own transfer before they
// return an error. The adapter contract never does that for a failed start, so the cases check that the function keeps the state the
// completion set, in case an adapter ever does.
class NestingContext : public USBDeviceContext {
public:
	Midi* target = nullptr;
	bool nestOutStart = false;
	bool nestInStart = false;
	uint32 outStarts = 0;


	ResultStatus OpenEndpoint(const USBDeviceAdapter::EndpointConfig&) override {
		return ResultStatus::ok;
	}


	ResultStatus CloseEndpoint(uint8) override {
		return ResultStatus::ok;
	}


	ResultStatus StartInTransfer(uint8 address, std::span<const uint8>, bool) override {
		if (nestInStart) {
			nestInStart = false;
			target->OnTransferComplete(address, 64, *this);
			return ResultStatus::notReady;
		}

		return ResultStatus::ok;
	}


	ResultStatus StartOutTransfer(uint8 address, std::span<uint8>) override {
		outStarts++;
		if (nestOutStart) {
			nestOutStart = false;
			target->OnTransferComplete(address, 0, *this);
			return ResultStatus::notReady;
		}

		return ResultStatus::ok;
	}


	ResultStatus Stall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus ClearStall(uint8) override {
		return ResultStatus::notReady;
	}


	ResultStatus RemoteWakeup() override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlSendData(std::span<const uint8>) override {
		return ResultStatus::notReady;
	}


	ResultStatus ControlReceiveData(std::span<uint8>) override {
		return ResultStatus::notReady;
	}
};


// The OUT start arms the endpoint, and its failure comes after a completion inside the call that re-armed it. The failure must keep that
// arm: the next Process() sees the endpoint armed and starts nothing. Clearing the flag would start a second transfer, so outStarts
// would be 3.
void TestNestedOutStartFailureKeepsArm() {
	Midi midi(Interfaces, Endpoints);
	NestingContext context;
	context.target = &midi;
	context.nestOutStart = true;

	SIM_EXPECT_EQ(midi.OnConfigured(context), ResultStatus::ok);
	SIM_EXPECT_EQ(context.outStarts, 2);

	midi.Process(context);
	SIM_EXPECT_EQ(context.outStarts, 2);
}


// The IN start fails after a completion inside the call committed both packets. They are the completion's, so the failure must not give
// them back to the queue. Returning them would send them to the host a second time.
void TestNestedInStartFailureKeepsCommittedPackets() {
	Midi midi(Interfaces, Endpoints);
	NestingContext context;
	context.target = &midi;

	SIM_EXPECT_EQ(midi.OnConfigured(context), ResultStatus::ok);
	SIM_EXPECT_EQ(midi.SendEvent(USBMidiPacket::Make(0, 0x9, 0x90, 0x3C, 0x64)), ResultStatus::ok);
	SIM_EXPECT_EQ(midi.SendEvent(USBMidiPacket::Make(0, 0x9, 0x90, 0x3E, 0x64)), ResultStatus::ok);

	context.nestInStart = true;
	midi.Process(context);
	SIM_EXPECT_EQ(midi.GetSendableCount(), 0);
}


// A packet on a cable the function does not have is dropped, so it reaches neither the RX queue nor the event handler. The packets
// on cables 0 and 1 around it are kept, in order.
void TestReceiveDropsOtherCables() {
	MidiFixture fixture;
	fixture.Configure();

	constexpr uint8 received[] = {
		0x09, 0x90, 0x3C, 0x64,
		0x59, 0x90, 0x3E, 0x64,
		0x19, 0x90, 0x40, 0x64
	};

	SIM_EXPECT_EQ(fixture.HostSend(received), ResultStatus::ok);
	fixture.device.Process();
	SIM_EXPECT_EQ(fixture.midi.GetReadableCount(), 2);

	USBMidiPacket packet;
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.bytes[0], 0x09);
	SIM_EXPECT_EQ(packet.bytes[2], 0x3C);
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::ok);
	SIM_EXPECT_EQ(packet.bytes[0], 0x19);
	SIM_EXPECT_EQ(packet.bytes[2], 0x40);
	SIM_EXPECT_EQ(fixture.midi.ReadEvent(packet), ResultStatus::notReady);

	SIM_EXPECT_EQ(fixture.recorder.eventCount, 2);
}


// The longest SysEx message is the whole queue: 96 bytes are 32 packets, and they are queued whole. A message of 97 bytes is 33 packets,
// which no queue of 32 can hold, so it is refused as invalid even when the queue is empty, not as busy.
void TestSysExLongestMessageFitsQueue() {
	MidiFixture fixture;
	fixture.Configure();

	std::array<uint8, 97> tooLong{};
	tooLong[0] = 0xF0;
	tooLong.back() = 0xF7;
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, tooLong), ResultStatus::invalidParameter);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);

	std::array<uint8, 96> longest{};
	longest[0] = 0xF0;
	longest.back() = 0xF7;
	SIM_EXPECT_EQ(fixture.midi.SendSysEx(0, longest), ResultStatus::ok);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 32);

	// 32 packets of 4 bytes, in two full transfers of 64 bytes.
	std::array<uint8, 512> in{};
	std::size_t count = fixture.ReadIn(in);
	SIM_EXPECT_EQ(count, 128);
	SIM_EXPECT_EQ(in[0], 0x04);
	SIM_EXPECT_EQ(in[1], 0xF0);
	SIM_EXPECT_EQ(fixture.midi.GetSendableCount(), 0);
}


// A transfer of 16 events is one full packet of 64 bytes. With nothing queued behind it, it ends with a zero-length packet, so the
// host's read ends there. The zero-length packet flips the DATA PID back, so the PID is as it was after the transfer.
void TestFullTransferWithNothingBehindEndsWithZeroLengthPacket() {
	MidiFixture fixture;
	fixture.Configure();

	for (uint8 note = 0; note < 16; note++) {
		SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, note, 1), ResultStatus::ok);
	}

	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	std::array<uint8, 64> in{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


// The same full transfer with an event behind it: the transfer ends without a zero-length packet, so the PID flips once, and the
// next transfer, one event of 4 bytes, starts at the completion and ends with a short packet, which flips the PID back.
void TestFullTransferWithEventBehindHasNoZeroLengthPacket() {
	MidiFixture fixture;
	fixture.Configure();

	for (uint8 note = 0; note < 17; note++) {
		SIM_EXPECT_EQ(fixture.midi.NoteOn(0, 0, note, 1), ResultStatus::ok);
	}

	fixture.device.Process();
	bool toggleBefore = fixture.sim.GetDataToggle(0x81);
	std::array<uint8, 64> in{};
	uint32 received = 0;
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, in, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 64);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) != toggleBefore);
	SIM_EXPECT(fixture.sim.HasArmedTransfer(0x81));

	std::array<uint8, 64> second{};
	SIM_EXPECT_EQ(fixture.sim.HostIn(0x81, second, received), ResultStatus::ok);
	SIM_EXPECT_EQ(received, 4);
	SIM_EXPECT(fixture.sim.GetDataToggle(0x81) == toggleBefore);
	SIM_EXPECT(!fixture.sim.HasArmedTransfer(0x81));
	SIM_EXPECT_EQ(fixture.device.GetErrorCount(), 0);
}


int main() {
	TestSysExLongestMessageFitsQueue();
	TestFullTransferWithNothingBehindEndsWithZeroLengthPacket();
	TestFullTransferWithEventBehindHasNoZeroLengthPacket();
	TestChannelHelpers();
	TestArgumentChecks();
	TestSysExSplit();
	TestSysExLongMessage();
	TestQueueLimits();
	TestSysExWholeOrNothing();
	TestReceivePath();
	TestReceiveBackPressure();
	TestResetClearsQueues();
	TestHaltReturnsInFlightPackets();
	TestOutHaltStopsArming();
	TestSendEventRejectsMalformed();
	TestReceiveSystemCommonNotSysEx();
	TestReceiveBackPressureThreshold();
	TestNextInTransferStartsFromCompletion();
	TestDeconfigureHidesReceivedEvents();
	TestHandlersBusyWhileConfigured();
	TestRefusedInStartKeepsPackets();
	TestSysExDataBytesAre7Bit();
	TestReceiveDropsOtherCables();
	TestSysExFramingRejected();
	TestNestedOutStartFailureKeepsArm();
	TestNestedInStartFailureKeepsCommittedPackets();
	return SimTest::Finish("TestMidi");
}
