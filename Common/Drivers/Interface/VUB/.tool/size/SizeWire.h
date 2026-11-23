#pragma once
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>


// Every size program runs SizeRun() once from main. Each configuration file and each baseline file defines it.
void SizeRun();


// The wire of the size programs: the bytes or the packet a link takes are kept here, and the run feeds them back to the receive side.
inline std::array<uint8, 128> sizeWire{};
inline std::size_t sizeWireLength = 0;


// The tick of the clock. Each read advances it, so the payload and the times change at run time and nothing is folded away.
inline uint32 sizeTicks = 0;


// The sum of the payloads the handler got. It is volatile, so the handler stays in every program.
inline volatile uint32 sizeReceived = 0;


inline uint32 SizeClock() {
	sizeTicks = sizeTicks + 1;
	return sizeTicks;
}


inline void SizeFill(std::span<uint8> out) {
	for (uint8& byte : out) {
		byte = static_cast<uint8>(SizeClock());
	}
}


inline void SizeKeep(std::span<const uint8> bytes) {
	for (uint8 byte : bytes) {
		if (sizeWireLength < sizeWire.size()) {
			sizeWire[sizeWireLength] = byte;
			sizeWireLength++;
		}
	}
}


inline void SizeReceive(const VubMeta&, std::span<const uint8> payload) {
	uint32 sum = 0;
	for (uint8 byte : payload) {
		sum = sum + byte;
	}

	sizeReceived = sizeReceived + sum;
}


// A byte-stream link. Write keeps the bytes in the wire and takes all of them.
struct SizeStreamLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).HalfDuplex().Starts(VubStarts::Either);


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		SizeKeep(bytes);
		return bytes.size();
	}
};


// A packet link. Send keeps the packet in the wire, in place of the one before it.
struct SizePacketLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		sizeWireLength = 0;
		SizeKeep(packet);
		return ResultStatus::ok;
	}
};


// The calls of a packet stack: send the payload, process, feed the packet in the wire back, and process.
template<typename Stack>
void SizeRunPacket(Stack& stack) {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	sizeWireLength = 0;
	(void)stack.Send(std::span<const uint8>(payload));
	stack.Process();
	stack.FeedPacket(std::span<const uint8>(sizeWire.data(), sizeWireLength));
	stack.Process();
}


// The calls of a byte-stream stack: send the payload, process, feed the bytes in the wire back one by one, and process.
template<typename Stack>
void SizeRunStream(Stack& stack) {
	std::array<uint8, 32> payload{};
	SizeFill(payload);
	sizeWireLength = 0;
	(void)stack.Send(std::span<const uint8>(payload));
	stack.Process();
	for (std::size_t index = 0; index < sizeWireLength; index++) {
		stack.Feed(sizeWire[index]);
	}

	stack.Process();
}
