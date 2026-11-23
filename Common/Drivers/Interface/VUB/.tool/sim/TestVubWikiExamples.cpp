#include "SimCheck.h"
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Receiver.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/TestKit/Loopback.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <optional>
#include <span>


// Every code snippet of the VUB wiki compiles here. The snippets are copied from .wiki/Common/Drivers/Interface/VUB/README.md, in the
// same order, each group in its own namespace because the names repeat.
namespace WikiWriting {
// A transform that appends one byte: the sum of the packet modulo 256. A packet whose sum does not match is refused.
struct SumCheck {
	static constexpr std::size_t growth = 1;
	static constexpr uint32 fingerprint = 0x53554D31;

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		uint8 sum = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
			sum = static_cast<uint8>(sum + in[index]);
		}

		out[in.size()] = sum;
		return in.size() + growth;
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		std::size_t body = packet.size() - 1;
		uint8 sum = 0;
		for (std::size_t index = 0; index < body; index++) {
			sum = static_cast<uint8>(sum + packet[index]);
		}

		if (sum != packet[body]) {
			return ResultStatus::checksumError;
		}

		return body;
	}
};

// The packet link of the board: it sends a whole packet, or reports busy when its transmitter has no room.
struct BoardLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}

void CheckContract() {
	SumCheck sumCheck;
	bool passes = VUB::CheckTransform(sumCheck, 7, 500, 32);
	SIM_EXPECT(passes);
}
}  // namespace WikiWriting


// The chain shape of the wiki. It uses the types of the Writing a block snippets.
namespace WikiChain {
using WikiWriting::BoardLink;
using WikiWriting::OnPacket;
using WikiWriting::SumCheck;

using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<SumCheck>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;
}  // namespace WikiChain


// The short names of the Names section: the namespace is opened inside the lambda only.
namespace WikiNames {
using WikiWriting::BoardLink;
using WikiWriting::OnPacket;

uint32 BoardClockUs() {
	return 0;
}


using Board = VUB::Stack<+[] {
	using namespace VUB;
	return Builder()
		.Layer<Reliable>().Retries(3).TimeoutMs(200)
		.Transform<Crc16Modbus>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>()
		.Clock<&BoardClockUs>();
}>;

BoardLink boardLink;
Board board(boardLink);
}  // namespace WikiNames


// A block with options and a Step, written only against the documented contract: its options are a nested type, its Step gives the
// option method after the block, and the block is built from its options.
namespace WikiSteps {
using WikiWriting::BoardLink;
using WikiWriting::OnPacket;

// A transform that XORs each byte with a key. Its options give the key, and its Step gives the key as a method right after it.
struct XorKey {
	struct Options {
		uint8 key = 0;
	};


	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}

		constexpr Step Key(uint8 value) const {
			Options options = this->LastOptions();
			options.key = value;
			return Step(this->WithLastOptions(options));
		}
	};

	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x584B4559;

	uint8 key = 0;

	XorKey() = default;

	explicit XorKey(const Options& options) : key(options.key) {
	}

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<XorKey>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

static_assert(VUB::Builder().Transform<XorKey>().Key(0x5A).LastOptions().key == 0x5A);
static_assert(VUB::Builder().Transform<XorKey>(XorKey::Options{0x5A}).LastOptions().key == 0x5A);
static_assert(VUB::Builder().Transform<XorKey>().LastOptions().key == 0);


// A transform that XORs each byte with a key given as a template argument. The stack stores it as Configured, so the key is a constant
// of the stored transform.
struct XorKeyOptions {
	uint8 key = 0;
};


template<XorKeyOptions Value = XorKeyOptions{}>
struct XorKeyOf {
	using Options = XorKeyOptions;

	template<Options value>
	using Configured = XorKeyOf<value>;

	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}

		constexpr Step Key(uint8 value) const {
			Options options = this->LastOptions();
			options.key = value;
			return Step(this->WithLastOptions(options));
		}
	};

	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x584B4559;
	static constexpr uint8 key = Value.key;

	Result<std::size_t> Encode(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = static_cast<uint8>(in[index] ^ key);
		}

		return in.size();
	}

	Result<std::size_t> Decode(const VUB::Meta&, std::span<uint8> packet) {
		for (std::size_t index = 0; index < packet.size(); index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ key);
		}

		return packet.size();
	}
};

using ConfiguredBoard = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<XorKeyOf<>>().Key(0x5A)
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

static_assert(VUB::Builder().Transform<XorKeyOf<>>().Key(0x5A).LastOptions().key == 0x5A);
static_assert(std::is_same_v<std::tuple_element_t<0, ConfiguredBoard::Storage>, XorKeyOf<XorKeyOptions{0x5A}>>);
}  // namespace WikiSteps


// A link that reports its state, a framing that resets, and a layer that is told of the connection and declares its peer.
namespace WikiLinkState {
using WikiWriting::OnPacket;


// A packet link that reports its state, as a USB device does. The stack gives its sink here; the interrupt reports the connection.
struct UsbLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex().ReportsState();

	// The stack gives its sink here, and the link reports its current state through it at once.
	void SetStateSink(VUB::StateSink value) {
		sink = value;
		sink.Report(configured);
	}

	// Called from the interrupt when the host configures or unconfigures the device. Report only records the state.
	void OnConfigured(bool value) {
		configured = value;
		sink.Report(value);
	}

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}

	VUB::StateSink sink;
	bool configured = false;
};


using Usb = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<UsbLink>()
		.MaxPayload(32)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&OnPacket>();
}>;


UsbLink usbLink;
Usb usb(usbLink);


void UsbSample() {
	usbLink.OnConfigured(true);
	usb.Process();
	SIM_EXPECT(usb.LinkUp());
	usbLink.OnConfigured(false);
	usb.Process();
	SIM_EXPECT(!usb.LinkUp());
	SIM_EXPECT(usb.LinkDowns() == 1);
}


// A framing that ends a frame at a newline byte, 0x0A. Growth is 1: the newline. A frame longer than 32 bytes is too long.
struct LineFraming {
	static constexpr bool usesTime = false;


	static constexpr std::size_t Growth(std::size_t length) {
		return length + 1;
	}


	Result<std::size_t> Frame(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.size() + 1 > out.size()) {
			return ResultStatus::messageTooLong;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		out[in.size()] = 0x0A;
		return in.size() + 1;
	}


	VUB::FrameStatus Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (byte != 0x0A) {
			if (count < body.size()) {
				body[count] = byte;
			}

			count++;
			return VUB::FrameStatus::pending;
		}

		std::size_t frame = count;
		count = 0;
		if (frame == 0) {
			return VUB::FrameStatus::pending;
		}

		if (frame > body.size() || frame > out.size()) {
			return VUB::FrameStatus::tooLong;
		}

		for (std::size_t index = 0; index < frame; index++) {
			out[index] = body[index];
		}

		length = frame;
		return VUB::FrameStatus::packet;
	}


	// Drops the partial frame. The stack calls it when the link goes up or down, if the framing has it.
	void Reset() {
		count = 0;
	}


	std::array<uint8, 32> body{};
	std::size_t count = 0;
};


using Lines = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<LineFraming>()
		.Link<VUB::StreamEnd<64, 512, true>>()
		.MaxPayload(32)
		.ReceiveRing(128)
		.OnReceive<&OnPacket>();
}>;

static_assert(Lines::count == 2);


// A layer that declares its peer down when nothing has been heard from it for 1000 microseconds. It is told of each change of the connection.
struct PeerWatch {
	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x50575431;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = false;
	static constexpr bool reportsPeer = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8>) {
		return VUB::Taken{0, true};
	}


	void Process(uint32 nowUs, const VUB::Outbox& outbox) {
		outbox.SetPeer(nowUs - heardUs < 1000);
	}


	// Called when a packet from the peer arrives.
	void Heard(uint32 nowUs) {
		heardUs = nowUs;
	}


	void OnLink(bool up, uint32 nowUs) {
		linkUp = up;
		changedUs = nowUs;
	}


	uint32 heardUs = 0;
	uint32 changedUs = 0;
	bool linkUp = false;
};


inline uint32 Now() {
	return 0;
}


using Watched = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<PeerWatch>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>()
		.Clock<&Now>();
}>;

static_assert(Watched::count == 2);
}  // namespace WikiLinkState


// The test kit's outages, and the pattern for a peer that reboots.
namespace WikiOutage {
using WikiLine = VUB::PacketLine<64, 512, true>;

using Near = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PacketEnd<64, 512, true>>()
		.MaxPayload(32)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&WikiWriting::OnPacket>()
		.Clock<&WikiLinkState::Now>();
}>;


void RebootPattern() {
	WikiLine line(VUB::Faults{});
	Near near(line.a);
	std::optional<Near> far;
	far.emplace(line.b);
	far.reset();
	line.SetLinkUp(false);
	near.Process();
	SIM_EXPECT(!near.LinkUp());
	line.SetLinkUp(true);
	far.emplace(line.b);
	near.Process();
	SIM_EXPECT(near.LinkUp());
	far->Process();
}


void OutageSample() {
	VUB::Faults faults;
	faults.seed = 7;
	faults.outageEveryUs = 500;
	faults.outageForUs = 100;
	WikiLine scheduled(faults);
	scheduled.SetNow(0);
	SIM_EXPECT(scheduled.toB.Counts().outaged == 0);
}
}  // namespace WikiOutage


namespace WikiDeliver {
// A layer that delivers in order. Each packet starts with a sequence number, one byte. A packet that comes early is held, and Process
// passes it up when the gap has filled. The layer holds one packet, for brevity.
struct InOrder {
	static constexpr std::size_t header = 1;
	static constexpr uint32 fingerprint = 0x494F5231;
	static constexpr bool usesTime = false;
	static constexpr bool makesBulk = false;
	static constexpr bool passesUp = true;


	Result<std::size_t> Add(const VUB::Meta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = sequence;
		sequence++;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VUB::Taken> Take(const VUB::Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		if (packet[0] == expected) {
			expected++;
			return VUB::Taken{header, true};
		}

		if (held || packet.size() - header > heldBody.size()) {
			return ResultStatus::busy;
		}

		heldLength = packet.size() - header;
		for (std::size_t index = 0; index < heldLength; index++) {
			heldBody[index] = packet[header + index];
		}

		heldNumber = packet[0];
		held = true;
		return VUB::Taken{header, false};
	}


	void Process(uint32, const VUB::Outbox& outbox) {
		if (held && heldNumber == expected) {
			held = false;
			expected++;
			(void)outbox.Deliver(std::span<const uint8>(heldBody.data(), heldLength));
		}
	}


	uint8 sequence = 0;
	uint8 expected = 0;
	bool held = false;
	uint8 heldNumber = 0;
	std::size_t heldLength = 0;
	std::array<uint8, 8> heldBody{};
};


// The payloads the handler got, in order.
inline std::array<uint8, 4> delivered{};
inline std::size_t deliveredCount = 0;


void OnInOrder(const VUB::Meta&, std::span<const uint8> payload) {
	if (deliveredCount < delivered.size() && !payload.empty()) {
		delivered[deliveredCount] = payload[0];
	}

	deliveredCount++;
}


// A packet link. The interrupt gives it packets with FeedPacket.
struct InOrderLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using InOrderStack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<InOrder>()
		.Link<InOrderLink>()
		.MaxPayload(8)
		.ReceiveQueue(4)
		.OnReceive<&OnInOrder>();
}>;


void InOrderSample() {
	InOrderLink link;
	InOrderStack stack(link);
	std::array<uint8, 2> early = {0x01, 0x11};
	std::array<uint8, 2> first = {0x00, 0x10};
	stack.FeedPacket(early);
	stack.FeedPacket(first);
	stack.Process();
}


// The sample: packet 0 goes up at once, and packet 1 goes up from Process after it.
void CheckInOrder() {
	InOrderSample();
	SIM_EXPECT(deliveredCount == 2);
	SIM_EXPECT(delivered[0] == 0x10);
	SIM_EXPECT(delivered[1] == 0x11);
}
}  // namespace WikiDeliver


// How a link hands received data to the stack: SetReceiver for a link that receives in an interrupt, Poll for a polled link.
namespace WikiReceive {
// A byte-stream link that receives in an interrupt. The stack gives it a receiver, and the interrupt feeds the receiver a byte at a time.
struct UartLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);

	// The stack gives its receiver here, and takes it back with a null receiver when it is destroyed.
	void SetReceiver(VUB::Receiver value) {
		receiver = value;
	}

	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}

	// Called from the receive interrupt. A byte that the ring has no room for is dropped and counted under ringFull.
	void OnByte(uint8 byte) {
		receiver.Feed(std::span<const uint8>(&byte, 1));
	}

	VUB::Receiver receiver;
};


// A packet link that is polled. Poll feeds the packets it has while the receive queue has room for them.
struct PollLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	// Called from Process, before the stack takes its received packets. Room is the number of free slots of the receive queue.
	void Poll(VUB::Receiver receiver) {
		while (packetsWaiting > 0 && receiver.Room() > 0) {
			std::array<uint8, 2> packet = {0x01, 0x02};
			receiver.FeedPacket(packet);
			packetsWaiting--;
		}
	}

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}

	std::size_t packetsWaiting = 0;
};


std::size_t receivedCount = 0;


void OnReceived(const VUB::Meta&, std::span<const uint8>) {
	receivedCount++;
}


using Uart = VUB::Stack<+[] {
	return VUB::Builder()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(32)
		.ReceiveRing(64)
		.OnReceive<&OnReceived>();
}>;


using Polled = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<PollLink>()
		.MaxPayload(32)
		.ReceiveQueue(2)
		.OnReceive<&OnReceived>();
}>;


UartLink uartLink;
Uart uart(uartLink);
PollLink pollLink;
Polled polled(pollLink);


// The interrupt feeds one COBS frame of the payload 01: the frame is 02 01 00. Process hands it up in the same call.
// The poll link has three packets and the queue holds two: Process polls two of them and takes them at once.
void ReceiveSample() {
	uartLink.OnByte(0x02);
	uartLink.OnByte(0x01);
	uartLink.OnByte(0x00);
	uart.Process();
	SIM_EXPECT(receivedCount == 1);

	pollLink.packetsWaiting = 3;
	polled.Process();
	SIM_EXPECT(pollLink.packetsWaiting == 1);
	SIM_EXPECT(receivedCount == 3);
	SIM_EXPECT(polled.Drops().queueFull == 0);
}
}  // namespace WikiReceive


int main() {
	WikiReceive::ReceiveSample();
	WikiWriting::CheckContract();
	WikiWriting::BoardSend();
	SIM_EXPECT(WikiWriting::board.Drops().sendError == 0);
	SIM_EXPECT(WikiChain::Board::count == 2);
	SIM_EXPECT(WikiSteps::Board::count == 2);
	WikiLinkState::UsbSample();
	WikiOutage::RebootPattern();
	WikiOutage::OutageSample();
	WikiDeliver::CheckInOrder();
	return SimFinish("TestVubWikiExamples");
}
