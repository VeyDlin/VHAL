#include <VHAL.h>
#include "Descriptors.h"
#include "FixedList.h"
#include "SimAssert.h"
#include "SimHost.h"
#include "SimUSBDeviceAdapter.h"
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Drivers/Interface/USB/Device/USBFunction.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

// Isochronous endpoints, at the adapter contract (SimUSBDeviceAdapter, USBDeviceAdapter.h "Isochronous endpoints") and through the device
// core with a function that streams on them. A frame is one host token: the test calls IsoInFrame() or IsoOutFrame() for it, after
// InjectStartOfFrame() when the start-of-frame events are on. The completion of a packet runs inside the frame's call, and the callbacks
// arm the next packet, as a function does. A packet armed from thread context is pending (USBDeviceAdapter.h): the first frame after the arm
// sends a zero-length packet with no completion, and the frame after that sends the packet. A packet armed from its own completion callback
// goes out at the next frame.


namespace {


// 128 bytes: two isochronous endpoints of this size take 2 * 2 * 128 = 512 bytes of the 832 that are left after the buffer table (64) and
// endpoint 0 (128), so both fit. The simulator places the blocks as the port does (SimUSBDeviceAdapter.h, FindBlock()). 200 bytes would take
// 2 * 2 * 224 = 896 and not fit beside the other one, on the port or here.
constexpr uint32 PacketSize = 128;
constexpr uint8 InAddress = 0x81;
constexpr uint8 OutAddress = 0x02;
// Number 3, not 2: endpoint number 2 is the isochronous OUT, and the two directions of a number share one register.
constexpr uint8 BulkInAddress = 0x83;


USBDeviceAdapter::EndpointConfig IsoConfig(uint8 address, uint16 size) {
	USBDeviceAdapter::EndpointConfig config;
	config.address = address;
	config.type = USBEndpointType::Isochronous;
	config.maxPacketSize = size;
	config.doubleBuffer = false;
	return config;
}


USBDeviceAdapter::EndpointConfig BulkConfig(uint8 address, uint16 size) {
	USBDeviceAdapter::EndpointConfig config = IsoConfig(address, size);
	config.type = USBEndpointType::Bulk;
	return config;
}


// The events of the adapter, in the order the tests read them: completions, errors and start-of-frame events.
struct IsoEvents {
	FixedList<uint8, 32> completionAddresses;
	FixedList<uint32, 32> completionCounts;
	FixedList<SimUSBDeviceAdapter::Error, 8> errors;
	FixedList<uint16, 32> frames;


	void OnTransferComplete(uint8 address, uint32 byteCount) {
		completionAddresses.Push(address);
		completionCounts.Push(byteCount);
	}


	void OnError(SimUSBDeviceAdapter::Error error) {
		errors.Push(error);
	}


	void OnStartOfFrame(uint16 frameNumber) {
		frames.Push(frameNumber);
	}
};


void Attach(SimUSBDeviceAdapter& sim, IsoEvents& events) {
	sim.onTransferComplete = VSTD::Delegate<void(uint8, uint32)>::Bind<IsoEvents, &IsoEvents::OnTransferComplete>(&events);
	sim.onError = VSTD::Delegate<void(SimUSBDeviceAdapter::Error)>::Bind<IsoEvents, &IsoEvents::OnError>(&events);
	sim.onStartOfFrame = VSTD::Delegate<void(uint16)>::Bind<IsoEvents, &IsoEvents::OnStartOfFrame>(&events);
}


// A packet of PacketSize bytes whose pattern depends on seed.
std::array<uint8, PacketSize> MakePacket(uint8 seed) {
	std::array<uint8, PacketSize> packet{};
	for (uint32 index = 0; index < PacketSize; ++index) {
		packet[index] = static_cast<uint8>((index * 13U + seed * 71U + 5U) & 0xFF);
	}

	return packet;
}


// Brings the adapter to a connected, reset state with the isochronous IN and OUT endpoints of the tests.
void PrepareIso(SimUSBDeviceAdapter& sim, IsoEvents& events, bool startOfFrameEvents) {
	Attach(sim, events);
	USBDeviceAdapter::Parameters parameters;
	parameters.startOfFrameEvents = startOfFrameEvents;
	SIM_EXPECT_EQ(sim.SetParameters(parameters), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	sim.InjectBusReset();
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoConfig(InAddress, PacketSize)), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.OpenEndpoint(IsoConfig(OutAddress, PacketSize)), ResultStatus::ok);
}


// The stream of the contract test. Each completion arms the next packet, one frame ahead of its token. When the list ends, the frames
// after it have nothing armed: an IN frame is a zero-length packet and an OUT packet is dropped.
struct IsoStream {
	std::array<std::array<uint8, PacketSize>, 3> packets{};
	std::array<std::array<uint8, 1024>, 3> sinks{};
	std::array<std::array<uint8, PacketSize>, 3> hostOut{};
	std::size_t nextIn = 0;
	std::size_t nextOut = 0;
	SimUSBDeviceAdapter* sim = nullptr;
	IsoEvents* events = nullptr;


	// The completion records the event, then arms the next packet of the stream in the same direction.
	void OnTransferComplete(uint8 address, uint32 byteCount) {
		events->completionAddresses.Push(address);
		events->completionCounts.Push(byteCount);
		if (address == InAddress && nextIn < packets.size()) {
			sim->StartInTransfer(InAddress, packets[nextIn], false);
			nextIn++;
		}

		if (address == OutAddress && nextOut < sinks.size()) {
			sim->StartOutTransfer(OutAddress, sinks[nextOut]);
			nextOut++;
		}
	}
};


void AttachStream(SimUSBDeviceAdapter& sim, IsoEvents& events, IsoStream& stream) {
	stream.sim = &sim;
	stream.events = &events;
	sim.onTransferComplete = VSTD::Delegate<void(uint8, uint32)>::Bind<IsoStream, &IsoStream::OnTransferComplete>(&stream);
}


// One packet per frame, both ways, across frames: the packets come out whole and in order, each with one completion of its size. The
// start-of-frame event of each frame is raised, and the frame numbers follow the injections. The first IN packet is armed from thread context,
// so it is pending: frame 0 is a zero-length packet, and the packets after it go out one frame behind their arm.
void TestIsoStreamsOnePacketPerFrame() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, true);

	IsoStream stream;
	stream.packets[0] = MakePacket(1);
	stream.packets[1] = MakePacket(2);
	stream.packets[2] = MakePacket(3);
	stream.hostOut[0] = MakePacket(4);
	AttachStream(sim, events, stream);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, stream.packets[0], false), ResultStatus::ok);
	stream.nextIn = 1;
	SIM_EXPECT_EQ(sim.StartOutTransfer(OutAddress, stream.sinks[0]), ResultStatus::ok);
	stream.nextOut = 1;

	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// Frame 0: the first IN packet is pending, so the token sends a zero-length packet and completes nothing. A full-size packet from the host on
	// OUT completes the OUT transfer, and its callback arms the next OUT buffer.
	sim.InjectStartOfFrame(0);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(events.completionCounts.count, 0U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, stream.hostOut[0]) == ResultStatus::ok);
	SIM_EXPECT(std::equal(stream.hostOut[0].begin(), stream.hostOut[0].end(), stream.sinks[0].begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), PacketSize);

	// Frame 1: the first IN packet goes out, and its completion arms the second from the callback, so that packet goes out at frame 2. OUT: a
	// zero-length packet, a completion of 0 bytes.
	sim.InjectStartOfFrame(1);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(stream.packets[0].begin(), stream.packets[0].end(), received.begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 2U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), PacketSize);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>()) == ResultStatus::ok);
	SIM_EXPECT_EQ(events.completionCounts.count, 3U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), 0U);

	// Frame 2: the second IN packet, which the completion of frame 1 armed. OUT: a short packet of 17 bytes.
	sim.InjectStartOfFrame(2);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(stream.packets[1].begin(), stream.packets[1].end(), received.begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 4U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>(stream.hostOut[0]).first(17)) == ResultStatus::ok);
	SIM_EXPECT_EQ(events.completionCounts.count, 5U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), 17U);

	// Frame 3: the last IN packet, which the completion of frame 2 armed.
	sim.InjectStartOfFrame(3);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(stream.packets[2].begin(), stream.packets[2].end(), received.begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 6U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), PacketSize);

	SIM_EXPECT_EQ(events.errors.count, 0U);
	SIM_EXPECT(!events.completionCounts.overflowed);
	SIM_EXPECT(!events.frames.overflowed);
	SIM_EXPECT_EQ(events.frames.count, 4U);
	SIM_EXPECT_EQ(events.frames[0], 0);
	SIM_EXPECT_EQ(events.frames[3], 3);
	SIM_EXPECT_EQ(sim.GetFrameNumber(), 3);
}


// A frame with nothing armed is answered with a zero-length packet. The host gets no data, nothing completes and no event is raised, and the
// frame is counted. The buffer select flips in that frame as it does in any other, so the packet armed afterwards goes out at a later token,
// and no packet of an earlier frame is sent again. An OUT packet of a frame with nothing armed is dropped, and the select flips for it too.
void TestIsoEmptyFrameSendsZeroLengthPacket() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);

	std::array<uint8, PacketSize> first = MakePacket(5);
	std::array<uint8, PacketSize> second = MakePacket(6);
	std::array<uint8, 1024> sink{};
	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// The first packet is armed from thread context, so it is pending. Frame 0 sends a zero-length packet with no completion, and the select
	// flips from false to true.
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, first, false), ResultStatus::ok);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(events.completionCounts.count, 0U);
	SIM_EXPECT(sim.GetIsoBufferSelect(InAddress));

	// Frame 1: the packet goes out and completes, and the select flips back to false.
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(first.begin(), first.end(), received.begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
	SIM_EXPECT(!sim.GetIsoBufferSelect(InAddress));

	// Frame 2: nothing armed. A zero-length packet, no completion, no event, and the select flips to true.
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(sim.GetMissedFrames(InAddress), 1U);
	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
	SIM_EXPECT_EQ(events.errors.count, 0U);
	SIM_EXPECT(sim.GetIsoBufferSelect(InAddress));

	// The OUT side: a packet of an armed frame completes it, and a frame after that with nothing armed drops its packet.
	SIM_EXPECT_EQ(sim.StartOutTransfer(OutAddress, sink), ResultStatus::ok);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, first) == ResultStatus::ok);
	SIM_EXPECT_EQ(events.completionCounts.count, 2U);
	SIM_EXPECT(sim.GetIsoBufferSelect(OutAddress));
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, second) == ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.GetMissedFrames(OutAddress), 1U);
	SIM_EXPECT_EQ(events.completionCounts.count, 2U);
	SIM_EXPECT(!sim.GetIsoBufferSelect(OutAddress));
	SIM_EXPECT(std::equal(first.begin(), first.end(), sink.begin()));

	// Frame 3: the second packet is armed from thread context, so this frame is its pending token: a zero-length packet, no completion, and the
	// select flips to false. Frame 4 sends the packet and flips the select to true.
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, second, false), ResultStatus::ok);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(events.completionCounts.count, 2U);
	SIM_EXPECT(!sim.GetIsoBufferSelect(InAddress));
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(second.begin(), second.end(), received.begin()));
	SIM_EXPECT_EQ(events.completionCounts.count, 3U);
	SIM_EXPECT(sim.GetIsoBufferSelect(InAddress));
	SIM_EXPECT_EQ(sim.GetMissedFrames(InAddress), 1U);
}


// An OUT packet that does not fit the armed buffer is dropped whole, with DataOverrun, and the transfer completes with 0 bytes.
void TestIsoOutOverrunDropsWholePacket() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);

	std::array<uint8, 100> small{};
	std::array<uint8, PacketSize> packet = MakePacket(7);
	SIM_EXPECT_EQ(sim.StartOutTransfer(OutAddress, small), ResultStatus::ok);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, packet) == ResultStatus::ok);
	SIM_EXPECT_EQ(events.errors.count, 1U);
	if (events.errors.count == 1) {
		SIM_EXPECT(events.errors.Back() == SimUSBDeviceAdapter::Error::DataOverrun);
	}

	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
	SIM_EXPECT_EQ(events.completionCounts.Back(), 0U);
	SIM_EXPECT(small[0] == 0);
}


// The arm and the frame rules: a packet longer than the endpoint's packet size is refused at the arm, a second arm is busy, and an IN
// frame whose out cannot hold the packet is refused and leaves the packet armed.
void TestIsoArmRules() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);

	std::array<uint8, 300> tooLong{};
	SIM_EXPECT(sim.StartInTransfer(InAddress, tooLong, false) == ResultStatus::invalidParameter);
	SIM_EXPECT(!sim.HasArmedTransfer(InAddress));

	std::array<uint8, PacketSize> packet = MakePacket(8);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::busy);

	// The packet is pending, so the first frame copies nothing: out needs no room, and the frame is a zero-length packet.
	std::array<uint8, 10> tiny{};
	uint32 length = 0;
	SIM_EXPECT(sim.IsoInFrame(InAddress, tiny, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(sim.HasArmedTransfer(InAddress));

	// The packet is in its buffer now, and the next frame would send it into out, which cannot hold it: refused, and the packet stays armed.
	SIM_EXPECT(sim.IsoInFrame(InAddress, tiny, length) == ResultStatus::invalidParameter);
	SIM_EXPECT(sim.HasArmedTransfer(InAddress));
	SIM_EXPECT_EQ(events.completionCounts.count, 0U);
}


// Stall() is refused on an isochronous endpoint, and ClearStall() is acknowledged and changes nothing: nothing is counted or abandoned, the
// buffer select stays, and an armed packet still goes out in a later frame.
void TestIsoStallRefusedClearStallAcknowledged() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);

	std::array<uint8, PacketSize> packet = MakePacket(9);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::ok);
	SIM_EXPECT(sim.Stall(InAddress) == ResultStatus::invalidParameter);
	bool selectBefore = sim.GetIsoBufferSelect(InAddress);
	SIM_EXPECT(sim.ClearStall(InAddress) == ResultStatus::ok);
	SIM_EXPECT(sim.GetIsoBufferSelect(InAddress) == selectBefore);
	SIM_EXPECT(!sim.IsStalled(InAddress));
	SIM_EXPECT_EQ(sim.GetStallCount(InAddress), 0U);
	SIM_EXPECT(sim.HasArmedTransfer(InAddress));
	SIM_EXPECT(sim.ClearStall(0x82) == ResultStatus::notExist);

	// The packet was armed from thread context, so the first frame sends a zero-length packet and the packet goes out at the second.
	std::array<uint8, 1024> received{};
	uint32 length = 0;
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(events.completionCounts.count, 0U);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
}


// The bulk host calls do not move isochronous packets, and the frame calls do not move bulk packets.
void TestIsoAndBulkCallsDoNotMix() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);
	SIM_EXPECT_EQ(sim.OpenEndpoint(BulkConfig(BulkInAddress, 64)), ResultStatus::ok);

	std::array<uint8, PacketSize> packet = MakePacket(10);
	std::array<uint8, 1024> received{};
	uint32 length = 0;
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::ok);
	SIM_EXPECT(sim.HostIn(InAddress, received, length) == ResultStatus::invalidParameter);
	SIM_EXPECT(sim.HasArmedTransfer(InAddress));

	SIM_EXPECT_EQ(sim.StartInTransfer(BulkInAddress, std::span<const uint8>(packet).first(64), false), ResultStatus::ok);
	SIM_EXPECT(sim.IsoInFrame(BulkInAddress, received, length) == ResultStatus::invalidParameter);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>()) == ResultStatus::notReady);
	SIM_EXPECT_EQ(sim.GetMissedFrames(OutAddress), 1U);
}


// Start-of-frame events: raised for each frame when the parameter is on, and not raised when it is off. The frame number is kept either way,
// because the controller counts frames whether or not the events are on. The number is 11 bits.
void TestIsoStartOfFrameEvents() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);
	sim.InjectStartOfFrame(5);
	SIM_EXPECT_EQ(events.frames.count, 0U);
	SIM_EXPECT_EQ(sim.GetFrameNumber(), 5);

	SimUSBDeviceAdapter sim2;
	IsoEvents events2;
	PrepareIso(sim2, events2, true);
	sim2.InjectStartOfFrame(0x0801);
	SIM_EXPECT_EQ(events2.frames.count, 1U);
	SIM_EXPECT_EQ(events2.frames.Back(), 1);
	SIM_EXPECT_EQ(sim2.GetFrameNumber(), 1);
}


// Disconnect abandons an armed packet with no event, and the endpoint carries packets again after the next connection.
void TestIsoDisconnectAbandonsPacket() {
	SimUSBDeviceAdapter sim;
	IsoEvents events;
	PrepareIso(sim, events, false);

	std::array<uint8, PacketSize> packet = MakePacket(11);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.Disconnect(), ResultStatus::ok);
	SIM_EXPECT(!sim.HasArmedTransfer(InAddress));
	SIM_EXPECT_EQ(events.completionCounts.count, 0U);

	// Armed again from thread context after the reconnect, so it is pending: the first frame is a zero-length packet, the second sends it.
	SIM_EXPECT_EQ(sim.Connect(), ResultStatus::ok);
	SIM_EXPECT_EQ(sim.StartInTransfer(InAddress, packet, false), ResultStatus::ok);
	std::array<uint8, 1024> received{};
	uint32 length = 0;
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT_EQ(events.completionCounts.count, 1U);
}


// A function that streams on the isochronous endpoints of the core. It opens them on configuration, keeps the OUT endpoint armed, and arms
// the next queued IN packet at each completion. The halt changes it sees are recorded, so a test can see that a refused one reached nothing.
class IsoStreamFunction : public USBFunction {
public:
	static constexpr std::size_t MaxQueued = 4;
	static constexpr uint8 Interfaces[1] = { 0 };
	static constexpr uint8 Endpoints[2] = { InAddress, OutAddress };


	FixedList<uint32, 8> inCompletions;
	FixedList<uint32, 8> outCompletions;
	FixedList<uint8, 8> haltChanges;
	std::array<uint8, 1024> outBuffer{};


private:
	std::array<std::span<const uint8>, MaxQueued> queue{};
	std::size_t queuedCount = 0;
	std::size_t nextQueued = 0;
	bool inArmed = false;


public:
	IsoStreamFunction()
		: USBFunction(Interfaces, Endpoints) {
	}


	// Queues one IN packet. It goes out in a later frame: Process() arms it, or the completion before it does.
	void Queue(std::span<const uint8> packet) {
		if (queuedCount < MaxQueued) {
			queue[queuedCount] = packet;
			queuedCount++;
		}
	}


	ResultStatus OnConfigured(USBDeviceContext& context) override {
		ResultStatus status = context.OpenEndpoint(IsoConfig(InAddress, PacketSize));
		if (status != ResultStatus::ok) {
			return status;
		}

		status = context.OpenEndpoint(IsoConfig(OutAddress, PacketSize));
		if (status != ResultStatus::ok) {
			return status;
		}

		status = context.StartOutTransfer(OutAddress, outBuffer);
		if (status != ResultStatus::ok) {
			return status;
		}

		ArmNextIn(context);
		return ResultStatus::ok;
	}


	void OnDeconfigured() override {
		inArmed = false;
	}


	void OnTransferComplete(uint8 address, uint32 byteCount, USBDeviceContext& context) override {
		if (address == InAddress) {
			inCompletions.Push(byteCount);
			inArmed = false;
			ArmNextIn(context);
			return;
		}

		outCompletions.Push(byteCount);
		context.StartOutTransfer(OutAddress, outBuffer);
	}


	void OnEndpointHaltChanged(uint8 address, bool, USBDeviceContext&) override {
		haltChanges.Push(address);
	}


	void Process(USBDeviceContext& context) override {
		if (!inArmed) {
			ArmNextIn(context);
		}
	}


private:
	void ArmNextIn(USBDeviceContext& context) {
		if (nextQueued >= queuedCount) {
			return;
		}

		inArmed = context.StartInTransfer(InAddress, queue[nextQueued], false) == ResultStatus::ok;
		if (inArmed) {
			nextQueued++;
		}
	}


};


// One interface with an isochronous IN 0x81 and an isochronous OUT 0x02, 128 bytes each with bInterval 1. wTotalLength 32.
inline constexpr uint8 IsoConfiguration[] = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0x80, 0x32,	// configuration: 32 bytes, 1 interface, value 1, bus powered, 100 mA
	0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x00, 0x00, 0x00,	// interface 0: vendor class, 2 endpoints
	0x07, 0x05, 0x81, 0x01, 0x80, 0x00, 0x01,				// endpoint 0x81: isochronous IN, 128 bytes, bInterval 1
	0x07, 0x05, 0x02, 0x01, 0x80, 0x00, 0x01				// endpoint 0x02: isochronous OUT, 128 bytes, bInterval 1
};


static_assert(sizeof(IsoConfiguration) == 32, "configuration is 32 bytes");


inline constexpr USBDescriptorSet IsoSet = {
	SimDescriptors::DeviceDescriptor,
	IsoConfiguration,
	SimDescriptors::StringTable,
	std::span<const uint8>(),
	std::span<const USBVendorDescriptor>()
};


// The core, with the function above: configuration opens the endpoints, the frames stream both ways, a frame with nothing armed is a
// zero-length packet that is counted, and the host's SET_FEATURE of ENDPOINT_HALT on the isochronous IN stalls while its CLEAR_FEATURE is
// acknowledged, and neither halts the endpoint. The packets queued from thread context are pending for one frame, and the completion of a
// packet arms the next one for the frame after it.
void TestCoreStreamsAndRefusesHalt() {
	SimUSBDeviceAdapter sim;
	IsoStreamFunction function;
	IsoEvents events;
	Attach(sim, events);
	USBFunction* functions[] = { &function };
	USBDevice device(sim, IsoSet, functions);
	USBDeviceAdapter::Parameters parameters;
	parameters.startOfFrameEvents = true;
	SIM_EXPECT_EQ(device.Start(parameters), ResultStatus::ok);
	sim.InjectBusReset();

	// Address, then configure (SET_CONFIGURATION is answered in the Address state only): the function opens the two endpoints and arms
	// the OUT endpoint. The IN packets are queued before the frames.
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 5, 5, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(SimHost::Control(sim, SimHost::MakeSetup(0x00, 9, 1, 0x0000, 0)), ResultStatus::ok);
	SIM_EXPECT_EQ(device.GetConfigurationValue(), 1);
	SIM_EXPECT(sim.HasArmedTransfer(OutAddress));

	std::array<uint8, PacketSize> first = MakePacket(12);
	std::array<uint8, PacketSize> second = MakePacket(13);
	std::array<uint8, PacketSize> third = MakePacket(14);
	function.Queue(first);
	function.Queue(second);
	device.Process();

	std::array<uint8, 1024> received{};
	uint32 length = 0;

	// Frame 0: the first packet was armed by Process(), outside the callback, so it is pending and this frame sends a zero-length packet. The
	// host's full-size packet on OUT completes the OUT transfer, and the function arms the next.
	sim.InjectStartOfFrame(0);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(function.inCompletions.count, 0U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, third) == ResultStatus::ok);
	SIM_EXPECT_EQ(function.outCompletions.count, 1U);
	SIM_EXPECT_EQ(function.outCompletions.Back(), PacketSize);

	// SET_FEATURE(ENDPOINT_HALT) on the IN endpoint is refused: the port's Stall() is invalidParameter, so the core stalls the request and
	// nothing changes. CLEAR_FEATURE(ENDPOINT_HALT) is acknowledged: ClearStall() is ok for an isochronous endpoint and changes nothing, and the
	// owner is told of the (empty) change, as for any other endpoint.
	std::array<uint8, 8> status{};
	uint32 statusLength = 0;
	SIM_EXPECT_EQ(
		SimHost::Control(sim, SimHost::MakeSetup(0x02, 3, 0, InAddress, 0), std::span<const uint8>(), std::span<uint8>(), statusLength),
		ResultStatus::accessError
	);
	SIM_EXPECT_EQ(function.haltChanges.count, 0U);
	SIM_EXPECT(!sim.IsStalled(InAddress));
	SIM_EXPECT_EQ(sim.GetStallCount(InAddress), 0U);
	SIM_EXPECT_EQ(
		SimHost::Control(sim, SimHost::MakeSetup(0x02, 1, 0, InAddress, 0), std::span<const uint8>(), std::span<uint8>(), statusLength),
		ResultStatus::ok
	);
	SIM_EXPECT_EQ(function.haltChanges.count, 1U);
	SIM_EXPECT_EQ(function.haltChanges.Back(), InAddress);
	SIM_EXPECT(!sim.IsStalled(InAddress));
	SIM_EXPECT_EQ(sim.GetStallCount(InAddress), 0U);

	// GET_STATUS on the endpoint answers 0: an isochronous endpoint is never halted.
	SIM_EXPECT_EQ(
		SimHost::Control(sim, SimHost::MakeSetup(0x82, 0, 0, InAddress, 2), std::span<const uint8>(), status, statusLength),
		ResultStatus::ok
	);
	SIM_EXPECT_EQ(statusLength, 2U);
	SIM_EXPECT_EQ(status[0], 0);

	// Frame 1: the first packet goes out, and the completion arms the second from the function's callback, so that one goes out at frame 2.
	// The OUT packet is short (17 bytes).
	sim.InjectStartOfFrame(1);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(first.begin(), first.end(), received.begin()));
	SIM_EXPECT_EQ(function.inCompletions.count, 1U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>(third).first(17)) == ResultStatus::ok);
	SIM_EXPECT_EQ(function.outCompletions.Back(), 17U);

	// Frame 2: the second packet, armed by the callback of frame 1, goes out. Nothing is queued after it. The OUT packet is zero-length.
	sim.InjectStartOfFrame(2);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(second.begin(), second.end(), received.begin()));
	SIM_EXPECT_EQ(function.inCompletions.count, 2U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>()) == ResultStatus::ok);
	SIM_EXPECT_EQ(function.outCompletions.Back(), 0U);

	// Frame 3: nothing queued, so the IN frame gets a zero-length packet with no completion, and it is counted. The OUT packet of the frame is
	// still taken: its completion is 0 bytes.
	sim.InjectStartOfFrame(3);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(sim.GetMissedFrames(InAddress), 1U);
	SIM_EXPECT_EQ(function.inCompletions.count, 2U);
	SIM_EXPECT(sim.IsoOutFrame(OutAddress, std::span<const uint8>()) == ResultStatus::ok);
	SIM_EXPECT_EQ(function.outCompletions.count, 4U);
	SIM_EXPECT_EQ(function.outCompletions.Back(), 0U);

	// Frame 4: a packet queued and armed by Process() is pending, so this frame sends a zero-length packet, and frame 5 sends the packet.
	function.Queue(third);
	device.Process();
	sim.InjectStartOfFrame(4);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, 0U);
	SIM_EXPECT_EQ(function.inCompletions.count, 2U);
	sim.InjectStartOfFrame(5);
	SIM_EXPECT(sim.IsoInFrame(InAddress, received, length) == ResultStatus::ok);
	SIM_EXPECT_EQ(length, PacketSize);
	SIM_EXPECT(std::equal(third.begin(), third.end(), received.begin()));

	SIM_EXPECT_EQ(function.inCompletions.count, 3U);
	SIM_EXPECT_EQ(function.outCompletions.count, 4U);
	SIM_EXPECT(!function.inCompletions.overflowed);
	SIM_EXPECT(!function.outCompletions.overflowed);
	SIM_EXPECT_EQ(events.frames.count, 6U);
	SIM_EXPECT_EQ(events.errors.count, 0U);
	SIM_EXPECT_EQ(device.GetErrorCount(), 0U);
	device.Stop();
}


}


int main() {
	TestIsoStreamsOnePacketPerFrame();
	TestIsoEmptyFrameSendsZeroLengthPacket();
	TestIsoOutOverrunDropsWholePacket();
	TestIsoArmRules();
	TestIsoStallRefusedClearStallAcknowledged();
	TestIsoAndBulkCallsDoNotMix();
	TestIsoStartOfFrameEvents();
	TestIsoDisconnectAbandonsPacket();
	TestCoreStreamsAndRefusesHalt();
	return SimTest::Finish("TestIsochronous");
}
