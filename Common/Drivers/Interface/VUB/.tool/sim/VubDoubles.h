#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubBytes.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Test doubles of the block kinds. They are not part of the library. Each is the smallest block that drives one part of the core,
// and each documents its wire format, so the tests can check bytes written by hand.


// Transform. Encrypted byte i is plain byte i XOR (0xA5 XOR (i * 37)). Two check bytes follow: the sum and the XOR of the encrypted
// bytes, both modulo 256. Growth 2. A flipped bit changes the sum, so it is always detected.
struct XorTransform {
	static constexpr std::size_t growth = 2;
	static constexpr uint32 fingerprint = 0x584F5231;


	static constexpr uint8 Key(std::size_t index) {
		return static_cast<uint8>(0xA5 ^ (index * 37));
	}


	Result<std::size_t> Encode(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + growth) {
			return ResultStatus::invalidParameter;
		}

		uint8 sum = 0;
		uint8 check = 0;
		for (std::size_t index = 0; index < in.size(); index++) {
			uint8 value = static_cast<uint8>(in[index] ^ Key(index));
			out[index] = value;
			sum = static_cast<uint8>(sum + value);
			check = static_cast<uint8>(check ^ value);
		}

		out[in.size()] = sum;
		out[in.size() + 1] = check;
		return in.size() + growth;
	}


	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		if (packet.size() < growth) {
			return ResultStatus::invalidData;
		}

		std::size_t body = packet.size() - growth;
		uint8 sum = 0;
		uint8 check = 0;
		for (std::size_t index = 0; index < body; index++) {
			sum = static_cast<uint8>(sum + packet[index]);
			check = static_cast<uint8>(check ^ packet[index]);
		}

		if (sum != packet[body] || check != packet[body + 1]) {
			return ResultStatus::checksumError;
		}

		for (std::size_t index = 0; index < body; index++) {
			packet[index] = static_cast<uint8>(packet[index] ^ Key(index));
		}

		return body;
	}
};


// A transform that changes nothing, and records the flags of the packet it encodes in testProbeFlags. Growth 0.
inline uint8 testProbeFlags = 0;
inline uint32 testProbeEncodes = 0;


struct FlagProbe {
	static constexpr std::size_t growth = 0;
	static constexpr uint32 fingerprint = 0x50524F42;


	Result<std::size_t> Encode(const VubMeta& meta, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size()) {
			return ResultStatus::invalidParameter;
		}

		testProbeFlags = meta.flags;
		testProbeEncodes++;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index] = in[index];
		}

		return in.size();
	}


	Result<std::size_t> Decode(const VubMeta&, std::span<uint8> packet) {
		return packet.size();
	}
};


// Framing with flags and escapes, in the style of HDLC. A frame is: flag 7E, the payload, the sum and the XOR of the payload (both
// modulo 256), then flag 7E. A byte 7E or 7D inside the frame is sent as 7D followed by the byte XOR 20. Growth is length + 6.
// A frame whose checks fail is broken, and the next flag starts the next frame, so one loss costs one frame at most.
struct FlagFraming {
	static constexpr bool usesTime = false;
	static constexpr uint8 Flag = 0x7E;
	static constexpr uint8 Escape = 0x7D;
	static constexpr std::size_t MaxBody = 257;


	static constexpr std::size_t Growth(std::size_t length) {
		return length + 6;
	}


	Result<std::size_t> Frame(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.size() > MaxBody - 2) {
			return ResultStatus::invalidParameter;
		}

		if (out.empty()) {
			return ResultStatus::messageTooLong;
		}

		out[0] = Flag;
		std::size_t length = 1;
		uint8 sum = 0;
		uint8 check = 0;
		for (uint8 value : in) {
			if (!Emit(out, length, value)) {
				return ResultStatus::messageTooLong;
			}

			sum = static_cast<uint8>(sum + value);
			check = static_cast<uint8>(check ^ value);
		}

		if (!Emit(out, length, sum) || !Emit(out, length, check)) {
			return ResultStatus::messageTooLong;
		}

		if (length + 1 > out.size()) {
			return ResultStatus::messageTooLong;
		}

		out[length] = Flag;
		return length + 1;
	}


	VubFrame Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (byte == Flag) {
			VubFrame result = VubFrame::pending;
			if (started && count > 0) {
				result = Finish(out, length);
			}

			started = true;
			count = 0;
			overflow = false;
			escaping = false;
			return result;
		}

		if (!started) {
			return VubFrame::pending;
		}

		if (escaping) {
			escaping = false;
			byte = static_cast<uint8>(byte ^ 0x20);
		} else if (byte == Escape) {
			escaping = true;
			return VubFrame::pending;
		}

		if (count < MaxBody) {
			body[count] = byte;
			count++;
		} else {
			overflow = true;
		}

		return VubFrame::pending;
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		started = false;
		count = 0;
		escaping = false;
		overflow = false;
	}


	std::array<uint8, MaxBody> body{};
	std::size_t count = 0;
	bool started = false;
	bool escaping = false;
	bool overflow = false;


private:
	static bool Emit(std::span<uint8> out, std::size_t& length, uint8 value) {
		if (value == Flag || value == Escape) {
			if (length + 2 > out.size()) {
				return false;
			}

			out[length] = Escape;
			out[length + 1] = static_cast<uint8>(value ^ 0x20);
			length += 2;
			return true;
		}

		if (length + 1 > out.size()) {
			return false;
		}

		out[length] = value;
		length++;
		return true;
	}


	VubFrame Finish(std::span<uint8> out, std::size_t& length) {
		if (overflow) {
			return VubFrame::tooLong;
		}

		if (count < 2) {
			return VubFrame::broken;
		}

		std::size_t payload = count - 2;
		uint8 sum = 0;
		uint8 check = 0;
		for (std::size_t index = 0; index < payload; index++) {
			sum = static_cast<uint8>(sum + body[index]);
			check = static_cast<uint8>(check ^ body[index]);
		}

		if (sum != body[payload] || check != body[payload + 1]) {
			return VubFrame::broken;
		}

		if (payload > out.size()) {
			return VubFrame::tooLong;
		}

		for (std::size_t index = 0; index < payload; index++) {
			out[index] = body[index];
		}

		length = payload;
		return VubFrame::packet;
	}
};


// Framing with a length byte in front: the byte is the payload length (1 to 255), then the payload. It does not resynchronise on its own:
// a partial frame takes the first bytes of the next one, so the stack must reset it when the link changes. Growth is length + 1.
struct LengthFraming {
	static constexpr bool usesTime = false;
	static constexpr std::size_t MaxBody = 64;


	static constexpr std::size_t Growth(std::size_t length) {
		return length + 1;
	}


	Result<std::size_t> Frame(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (in.size() == 0 || in.size() > MaxBody || in.size() + 1 > out.size()) {
			return ResultStatus::messageTooLong;
		}

		out[0] = static_cast<uint8>(in.size());
		for (std::size_t index = 0; index < in.size(); index++) {
			out[index + 1] = in[index];
		}

		return in.size() + 1;
	}


	VubFrame Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		if (!haveLength) {
			expected = byte;
			haveLength = true;
			count = 0;
			return VubFrame::pending;
		}

		if (count < MaxBody) {
			body[count] = byte;
		}

		count++;
		if (count < expected) {
			return VubFrame::pending;
		}

		haveLength = false;
		if (expected > MaxBody || expected > out.size()) {
			return VubFrame::tooLong;
		}

		for (std::size_t index = 0; index < expected; index++) {
			out[index] = body[index];
		}

		length = expected;
		return VubFrame::packet;
	}


	// Drops a partial frame. The stack calls it when the link changes state.
	void Reset() {
		haveLength = false;
		count = 0;
	}


	std::array<uint8, MaxBody> body{};
	std::size_t expected = 0;
	std::size_t count = 0;
	bool haveLength = false;
};


// The options of TestLayer. beaconIntervalUs sends a beacon from Process every interval (0 sends none). bulk makes Next return a
// bulk packet every time, so the stack always has bulk data to pull. bulkLimit stops the bulk packets after that many (0 is no
// limit), so a test that loops on the bulk data ends with a wrong count instead of never ending.
struct TestLayerOptions {
	uint32 beaconIntervalUs = 0;
	bool bulk = false;
	uint32 bulkLimit = 0;
};


// Counters the tests read. The stack does not expose its blocks, so the layer counts into these.
inline uint32 testBeaconsSent = 0;
inline uint32 testBeaconsTaken = 0;


// Layer. Header, 3 bytes: kind (0 data, 1 beacon, 2 bulk), then a sequence number, 16 bits little-endian. A data packet is delivered
// upwards with its header removed; a beacon is taken and not delivered; a bulk packet is delivered like data. The layer uses time
// for its beacons, so a chain with it needs a clock.
struct TestLayer {
	static constexpr std::size_t header = 3;
	static constexpr uint32 fingerprint = 0x544C5931;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = true;
	static constexpr uint8 KindData = 0;
	static constexpr uint8 KindBeacon = 1;
	static constexpr uint8 KindBulk = 2;


	TestLayer() = default;


	explicit TestLayer(const TestLayerOptions& value) : options(value) {
	}


	Result<std::size_t> Add(const VubMeta&, std::span<const uint8> in, std::span<uint8> out) {
		if (out.size() < in.size() + header) {
			return ResultStatus::invalidParameter;
		}

		out[0] = KindData;
		VubPutLe16(out.data() + 1, sequence);
		sequence++;
		for (std::size_t index = 0; index < in.size(); index++) {
			out[header + index] = in[index];
		}

		return in.size() + header;
	}


	Result<VubTake> Take(const VubMeta&, std::span<const uint8> packet) {
		if (packet.size() < header) {
			return ResultStatus::invalidData;
		}

		if (packet[0] == KindBeacon) {
			testBeaconsTaken++;
			return VubTake{header, false};
		}

		return VubTake{header, true};
	}


	void Process(uint32 nowUs, const VubOutbox& outbox) {
		if (options.beaconIntervalUs == 0 || nowUs < nextBeaconUs) {
			return;
		}

		std::array<uint8, header> beacon{};
		beacon[0] = KindBeacon;
		VubPutLe16(beacon.data() + 1, sequence);
		VubMeta meta{};
		if (outbox.Send(std::span<const uint8>(beacon.data(), beacon.size()), meta) == ResultStatus::ok) {
			sequence++;
			nextBeaconUs = nowUs + options.beaconIntervalUs;
			testBeaconsSent++;
		}
	}


	Result<std::size_t> Next(VubMeta& meta, std::span<uint8> packet) {
		if (!options.bulk) {
			return ResultStatus::noDataAvailable;
		}

		if (options.bulkLimit != 0 && bulkPulled >= options.bulkLimit) {
			return ResultStatus::noDataAvailable;
		}

		if (packet.size() < header + 1) {
			return ResultStatus::invalidParameter;
		}

		bulkPulled++;
		packet[0] = KindBulk;
		VubPutLe16(packet.data() + 1, sequence);
		sequence++;
		packet[header] = 0xBB;
		meta.flags = static_cast<uint8>(meta.flags | VubMeta::NoAcknowledgeFlag);
		return header + 1;
	}


	TestLayerOptions options;
	uint16 sequence = 0;
	uint32 nextBeaconUs = 0;
	uint32 bulkPulled = 0;
};


// The flag framing with a refusal: a packet with the no-acknowledge flag (the bulk packets of TestLayer) is refused with an error, and
// the other packets are framed as FlagFraming frames them. Used to show that a packet the framing refuses still counts toward
// PerProcess.
struct BulkRefusingFraming {
	static constexpr bool usesTime = false;


	static constexpr std::size_t Growth(std::size_t length) {
		return FlagFraming::Growth(length);
	}


	Result<std::size_t> Frame(const VubMeta& meta, std::span<const uint8> in, std::span<uint8> out) {
		if ((meta.flags & VubMeta::NoAcknowledgeFlag) != 0) {
			return ResultStatus::invalidParameter;
		}

		return inner.Frame(meta, in, out);
	}


	VubFrame Push(uint8 byte, std::span<uint8> out, std::size_t& length) {
		return inner.Push(byte, out, length);
	}


	FlagFraming inner;
};
