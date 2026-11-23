#pragma once
#include <Drivers/Interface/VUB/TestKit/VubWire.h>
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>


// The link objects of the test kit. Each end writes into a wire of its line, and the other end's stack takes the bytes or packets with
// the line's DeliverTo calls. A round budget stands in for a busy link: StartRound sets how much the end accepts before the next
// round, and nothing is kept on the wire when the budget runs out. The stack keeps what the link refused.


// A byte-stream end. Write accepts up to the round budget and returns how many bytes it took, as a USB class does.
template<std::size_t MaxPacket, std::size_t Capacity>
class VubStreamEnd {
public:
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(MaxPacket).HalfDuplex().Starts(VubStarts::Either);


	explicit VubStreamEnd(VubStreamWire<Capacity>& outgoing) : wire(&outgoing) {
	}


	// Sets how many bytes the end accepts until the next StartRound.
	void StartRound(std::size_t bytes) {
		budget = bytes;
	}


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		std::size_t accepted = bytes.size() < budget ? bytes.size() : budget;
		wire->Push(bytes.first(accepted));
		budget -= accepted;
		return accepted;
	}


private:
	VubStreamWire<Capacity>* wire;
	std::size_t budget = static_cast<std::size_t>(-1);
};


// A packet end. Send takes a whole packet while the round budget lasts, and returns busy after that. A packet longer than the line's
// MaxPacket is refused with messageTooLong, as the link would refuse it: the wire never cuts a packet short.
template<std::size_t MaxPacket, std::size_t Capacity>
class VubPacketEnd {
public:
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(MaxPacket).FullDuplex();


	explicit VubPacketEnd(VubPacketWire<Capacity, MaxPacket>& outgoing) : wire(&outgoing) {
	}


	// Sets how many packets the end accepts until the next StartRound.
	void StartRound(std::size_t packets) {
		budget = packets;
	}


	ResultStatus Send(std::span<const uint8> packet) {
		if (packet.size() > MaxPacket) {
			return ResultStatus::messageTooLong;
		}

		if (budget == 0) {
			return ResultStatus::busy;
		}

		budget--;
		wire->Push(packet);
		return ResultStatus::ok;
	}


private:
	VubPacketWire<Capacity, MaxPacket>* wire;
	std::size_t budget = static_cast<std::size_t>(-1);
};


// A byte-stream line: two wires and the two ends. The stack on end a is fed by toA, the one on end b by toB.
template<std::size_t MaxPacket, std::size_t Capacity>
class VubStreamLine {
public:
	explicit VubStreamLine(const VubFaults& faults) : toB(faults, faults.seed), toA(faults, faults.seed ^ 0x5A5A5A5Au), a(toB), b(toA) {
	}


	void SetNow(uint32 nowUs) {
		toB.SetNow(nowUs);
		toA.SetNow(nowUs);
	}


	template<typename Sink>
	void DeliverToB(Sink& sink) {
		toB.Deliver(sink);
	}


	template<typename Sink>
	void DeliverToA(Sink& sink) {
		toA.Deliver(sink);
	}


	VubStreamWire<Capacity> toB;
	VubStreamWire<Capacity> toA;
	VubStreamEnd<MaxPacket, Capacity> a;
	VubStreamEnd<MaxPacket, Capacity> b;
};


// A packet line, as VubStreamLine, for packets of up to MaxPacket bytes.
template<std::size_t MaxPacket, std::size_t Capacity>
class VubPacketLine {
public:
	explicit VubPacketLine(const VubFaults& faults) : toB(faults, faults.seed), toA(faults, faults.seed ^ 0x5A5A5A5Au), a(toB), b(toA) {
	}


	void SetNow(uint32 nowUs) {
		toB.SetNow(nowUs);
		toA.SetNow(nowUs);
	}


	template<typename Sink>
	void DeliverToB(Sink& sink) {
		toB.Deliver(sink);
	}


	template<typename Sink>
	void DeliverToA(Sink& sink) {
		toA.Deliver(sink);
	}


	VubPacketWire<Capacity, MaxPacket> toB;
	VubPacketWire<Capacity, MaxPacket> toA;
	VubPacketEnd<MaxPacket, Capacity> a;
	VubPacketEnd<MaxPacket, Capacity> b;
};
