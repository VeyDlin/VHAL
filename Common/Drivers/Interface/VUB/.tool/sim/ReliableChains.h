#pragma once
#include "Chains.h"
#include "Doubles.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Chains and bookkeeping of the delivery layer's tests. Every payload starts with a 16-bit id, little-endian, so the handlers count
// each packet by its id. The clock is testNowUs of Chains.h.


// The largest id the bookkeeping counts. Ids of this value and above are not counted.
inline constexpr std::size_t ReliableIds = 512;


// How many times each id was delivered to the receive handler, how many outcomes it got, the last status and the time of the outcome.
inline std::array<uint16, ReliableIds> testDelivered{};
inline std::array<uint16, ReliableIds> testOutcomes{};
inline std::array<ResultStatus, ReliableIds> testStatus{};
inline std::array<uint32, ReliableIds> testOutcomeAtUs{};
inline std::array<uint16, ReliableIds> testAttempts{};
inline uint32 testRestarts = 0;


// A hook the test sets to run inside the receive handler, for a reply sent from the handler.
inline void (*testDeliverHook)(uint16 id) = nullptr;


inline uint16 ReliableIdOf(std::span<const uint8> payload) {
	return VUB::GetLe16(payload.data());
}


inline void ReliableReceive(const VUB::Meta&, std::span<const uint8> payload) {
	uint16 id = ReliableIdOf(payload);
	if (id < ReliableIds) {
		testDelivered[id]++;
	}

	if (testDeliverHook != nullptr) {
		testDeliverHook(id);
	}
}


inline void ReliableOutcome(const VUB::Outcome& outcome) {
	uint16 id = ReliableIdOf(outcome.payload);
	if (id < ReliableIds) {
		testOutcomes[id]++;
		testStatus[id] = outcome.status;
		testOutcomeAtUs[id] = testNowUs;
		testAttempts[id] = outcome.attempts;
	}
}


inline void ReliableRestart() {
	testRestarts++;
}


inline void ResetReliableState() {
	ResetTestState();
	testDelivered.fill(0);
	testOutcomes.fill(0);
	testStatus.fill(ResultStatus::ok);
	testOutcomeAtUs.fill(0);
	testAttempts.fill(0);
	testRestarts = 0;
	testDeliverHook = nullptr;
}


// The packets a line carried, kept as bytes, for the tests that check the bytes on a wire.
struct ReliableCapture {
	std::array<std::array<uint8, 64>, 8> packets{};
	std::array<std::size_t, 8> lengths{};
	std::size_t count = 0;


	void Feed(uint8) {
	}


	void FeedPacket(std::span<const uint8> packet) {
		if (count < packets.size()) {
			for (std::size_t index = 0; index < packet.size(); index++) {
				packets[count][index] = packet[index];
			}

			lengths[count] = packet.size();
		}

		count++;
	}
};


// True when packet index of the capture is exactly the expected bytes.
template<std::size_t Size>
bool CaptureIs(const ReliableCapture& capture, std::size_t index, const std::array<uint8, Size>& expected) {
	if (index >= capture.count || index >= capture.packets.size() || capture.lengths[index] != Size) {
		return false;
	}

	for (std::size_t byteIndex = 0; byteIndex < Size; byteIndex++) {
		if (capture.packets[index][byteIndex] != expected[byteIndex]) {
			return false;
		}
	}

	return true;
}


// A payload of 8 bytes that starts with the id.
inline std::array<uint8, 8> ReliablePayload(uint16 id) {
	std::array<uint8, 8> bytes{};
	VUB::PutLe16(bytes.data(), id);
	for (std::size_t index = 2; index < bytes.size(); index++) {
		bytes[index] = static_cast<uint8>(id + index);
	}

	return bytes;
}


// Sends the payload of id through the stack, with flags in its meta.
template<typename Stack>
ResultStatus ReliableSend(Stack& stack, uint16 id, uint8 flags = 0) {
	std::array<uint8, 8> bytes = ReliablePayload(id);
	VUB::Meta meta{};
	meta.flags = flags;
	return stack.Send(std::span<const uint8>(bytes.data(), bytes.size()), meta);
}


// The layer under test: payloads up to 40 bytes, up to 4 packets in flight.
using ReliableLayer = VUB::ReliableOf<40, 4>;


// The session value the tests give each stack: the application gives its layer a value once per boot, before the first send. The wire
// tests expect the bytes of this value (their clock reads 0x1234).
inline constexpr uint16 ReliableSession = 0x1234;


// Gives the layer of a stack its session value, as the application does before the first send.
template<typename Stack>
void GiveSession(Stack& stack, uint16 value = ReliableSession) {
	stack.template Find<ReliableLayer>().SetSession(value);
}


// Packet chain, no transform, a line that does not report. Used for the wire bytes and the timing.
using ReliableWire = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// The same with 8-bit sequence numbers.
using ReliableWire8 = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).SequenceBits(8).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// The same as ReliableWire with a backoff: the resend timeout doubles after each try, up to 800 ms.
using ReliableBackoff = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).BackoffMaxMs(800).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Link<PacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// Packet chain with the XOR check transform, over a line that reports its state.
using ReliablePacket = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// The 8-bit sequence chain over a reporting packet line, with the XOR transform.
using ReliablePacket8 = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).SequenceBits(8).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;


// Stream chain: the XOR check transform and the flag framing, over a line that reports its state. Used with byte faults.
using ReliableStream = VUB::Stack<+[] {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<ReliableLayer>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(40).OnOutcome<&ReliableOutcome>().OnRestart<&ReliableRestart>()
		.Transform<XorTransform>()
		.Framing<FlagFraming>()
		.Link<ReportingStreamEnd>()
		.MaxPayload(40)
		.ReceiveRing(256)
		.OnReceive<&ReliableReceive>()
		.Clock<&TestClock>();
}>;
