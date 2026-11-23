#pragma once
#include "VubDoubles.h"
#include <Drivers/Interface/VUB/TestKit/VubLoopback.h>
#include <Drivers/Interface/VUB/Vub.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <span>


// The clock of the test chains. Tests set testNowUs.
inline uint32 testNowUs = 0;


inline uint32 TestClock() {
	return testNowUs;
}


// The payloads the receive handler was given, in order. The first 256 are kept; testReceivedCount counts all of them.
inline std::array<std::array<uint8, 64>, 256> testReceived{};
inline std::array<std::size_t, 256> testReceivedLength{};
inline std::size_t testReceivedCount = 0;


inline void TestReceive(const VubMeta&, std::span<const uint8> payload) {
	if (testReceivedCount < 256) {
		for (std::size_t index = 0; index < payload.size(); index++) {
			testReceived[testReceivedCount][index] = payload[index];
		}

		testReceivedLength[testReceivedCount] = payload.size();
	}

	testReceivedCount++;
}


// A sink that keeps the bytes and packets it is given, for checking what a wire carried.
struct RecordSink {
	std::array<uint8, 1024> bytes{};
	std::size_t byteCount = 0;
	std::array<std::size_t, 1024> packetLength{};
	std::size_t packetCount = 0;


	void Feed(uint8 byte) {
		if (byteCount < bytes.size()) {
			bytes[byteCount] = byte;
		}

		byteCount++;
	}


	void FeedPacket(std::span<const uint8> packet) {
		if (packetCount < packetLength.size()) {
			packetLength[packetCount] = packet.size();
		}

		packetCount++;
	}
};


// Clears the counters the chains and the doubles keep, so each test starts from zero.
inline void ResetTestState() {
	testNowUs = 0;
	testReceivedCount = 0;
	testBeaconsSent = 0;
	testBeaconsTaken = 0;
	testProbeFlags = 0;
	testProbeEncodes = 0;
}


using StreamEnd = VubStreamEnd<64, 512>;
using PacketEnd = VubPacketEnd<64, 512>;
using StreamLine = VubStreamLine<64, 512>;
using PacketLine = VubPacketLine<64, 512>;


// Layer, XOR transform, flag framing, stream link. Payload 40, packet 40 + 3 + 2 = 45, framed 2 * 45 + 6 = 96 bytes.
using StreamStack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same stream chain with a beacon every 1000 microseconds.
using BeaconStack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>(TestLayerOptions{1000, false})
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Layer, XOR transform, packet link. Packet 45 bytes, read into a queue of 4 packets.
using PacketStack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The packet chain whose layer always has bulk data to pull.
using BulkStack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>(TestLayerOptions{0, true})
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The lines that report their state, as a USB class does: the stacks on them are told when the line goes down and up.
using ReportingStreamEnd = VubStreamEnd<64, 512, true>;
using ReportingPacketEnd = VubPacketEnd<64, 512, true>;
using ReportingStreamLine = VubStreamLine<64, 512, true>;
using ReportingPacketLine = VubPacketLine<64, 512, true>;


// Flag framing over a reporting stream line, with no layer: a frame is the payload with its checks and flags. Waits while the link is down.
using ReportingStreamStack = Vub<+[] {
	return VubBuilder()
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Flag framing over a line that does not report: the stack never knows the line went down.
using SilentStreamStack = Vub<+[] {
	return VubBuilder()
		.Framing<FlagFraming>()
		.Link<StreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// Length framing over a reporting stream line. The waiting chain keeps what is queued while the link is down; the failing one drops it.
using ReportingLengthStack = Vub<+[] {
	return VubBuilder()
		.Framing<LengthFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


using FailingLengthStack = Vub<+[] {
	return VubBuilder()
		.Framing<LengthFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.WhileDown(VubDown::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same chain, which drops what is queued when the link goes down.
using FailingStreamStack = Vub<+[] {
	return VubBuilder()
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.WhileDown(VubDown::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A packet chain with no layer over a reporting packet line. Waits while the link is down.
using ReportingPacketStack = Vub<+[] {
	return VubBuilder()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The same chain, which drops what is queued when the link goes down.
using FailingPacketStack = Vub<+[] {
	return VubBuilder()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.WhileDown(VubDown::Fail)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// A packet chain with the flag probe under the layer, so the flags set at the top reach a transform at the bottom.
using ProbeStack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>(TestLayerOptions{0, true})
		.Transform<FlagProbe>()
		.Transform<XorTransform>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;
