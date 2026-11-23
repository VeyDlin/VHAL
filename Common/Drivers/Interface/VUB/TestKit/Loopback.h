#pragma once
#include <Drivers/Interface/VUB/TestKit/Wire.h>
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>

namespace VUB {


// The link objects of the test kit. Each end writes into a wire of its line, and the other end's stack takes the bytes or packets with
// the line's DeliverTo calls. A round budget stands in for a busy link: StartRound sets how much the end accepts before the next
// round, and nothing is kept on the wire when the budget runs out. The stack keeps what the link refused.
//
// The Reports parameter gives an end the state trait: the end reports the line's state to its stack, as a USB class does. An end
// without it is silent, as a UART is: what is sent while the line is down is lost, and the stack is not told.


// The traits of a byte-stream end: its MaxPacket and duplex, and the state trait when the end reports.
template<std::size_t MaxPacket, bool Reports>
constexpr LinkTraits StreamEndTraits() {
	LinkTraits traits = LinkTraits().MaxPacket(MaxPacket).HalfDuplex().Starts(Initiator::Either);
	if constexpr (Reports) {
		return traits.ReportsState();
	} else {
		return traits;
	}
}


// The traits of a packet end: framed by the packet, full duplex, and the state trait when the end reports.
template<std::size_t MaxPacket, bool Reports>
constexpr LinkTraits PacketEndTraits() {
	LinkTraits traits = LinkTraits().Framed().MaxPacket(MaxPacket).FullDuplex();
	if constexpr (Reports) {
		return traits.ReportsState();
	} else {
		return traits;
	}
}


// A byte-stream end. Write accepts up to the round budget and returns how many bytes it took, as a USB class does.
template<std::size_t MaxPacket, std::size_t Capacity, bool Reports = false>
class StreamEnd {
public:
	static constexpr LinkTraits traits = StreamEndTraits<MaxPacket, Reports>();


	explicit StreamEnd(StreamWire<Capacity>& outgoing) : wire(&outgoing) {
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


	// Gives the end the sink of its stack, and reports the end's state into it.
	void SetStateSink(StateSink value) requires (Reports) {
		sink = value;
		sink.Report(up);
	}


	// Sets the state the end reports to its stack. The line calls it.
	void SetUp(bool value) requires (Reports) {
		up = value;
		sink.Report(up);
	}


private:
	StreamWire<Capacity>* wire;
	std::size_t budget = static_cast<std::size_t>(-1);
	StateSink sink;
	bool up = true;
};


// A packet end. Send takes a whole packet while the round budget lasts, and returns busy after that. A packet longer than the line's
// MaxPacket is refused with messageTooLong, as the link would refuse it: the wire never cuts a packet short.
template<std::size_t MaxPacket, std::size_t Capacity, bool Reports = false>
class PacketEnd {
public:
	static constexpr LinkTraits traits = PacketEndTraits<MaxPacket, Reports>();


	explicit PacketEnd(PacketWire<Capacity, MaxPacket>& outgoing) : wire(&outgoing) {
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


	// Gives the end the sink of its stack, and reports the end's state into it.
	void SetStateSink(StateSink value) requires (Reports) {
		sink = value;
		sink.Report(up);
	}


	// Sets the state the end reports to its stack. The line calls it.
	void SetUp(bool value) requires (Reports) {
		up = value;
		sink.Report(up);
	}


private:
	PacketWire<Capacity, MaxPacket>* wire;
	std::size_t budget = static_cast<std::size_t>(-1);
	StateSink sink;
	bool up = true;
};


// A byte-stream line: two wires and the two ends. The stack on end a is fed by toA, the one on end b by toB. SetLinkUp takes the line down
// or up: both wires lose what is sent while it is down, and a reporting line tells both ends. A line with faults.outageEveryUs runs a
// schedule of outages from the seed, through SetNow.
template<std::size_t MaxPacket, std::size_t Capacity, bool Reports = false>
class StreamLine {
public:
	explicit StreamLine(const Faults& faults) : toB(faults, faults.seed), toA(faults, faults.seed ^ 0x5A5A5A5Au), a(toB), b(toA),
		schedule(faults) {
	}


	// Advances the schedule to nowUs. The schedule changes only the schedule's own state: a line taken down by hand stays down.
	void SetNow(uint32 nowUs) {
		toB.SetNow(nowUs);
		toA.SetNow(nowUs);
		scheduleUp = schedule.UpAt(nowUs);
		ApplyLink();
	}


	// Takes the line down or up by hand. A line taken down by hand stays down until it is taken up by hand, and a line taken up is still
	// down while its schedule has it down. Setting the state the line has is no change.
	void SetLinkUp(bool up) {
		manualUp = up;
		ApplyLink();
	}


	template<typename Sink>
	void DeliverToB(Sink& sink) {
		toB.Deliver(sink);
	}


	template<typename Sink>
	void DeliverToA(Sink& sink) {
		toA.Deliver(sink);
	}


	StreamWire<Capacity> toB;
	StreamWire<Capacity> toA;
	StreamEnd<MaxPacket, Capacity, Reports> a;
	StreamEnd<MaxPacket, Capacity, Reports> b;
	OutageSchedule schedule;


private:
	// The line is up when it is up by hand and up by its schedule. A change of that is applied to both wires and, on a reporting line, to
	// both ends.
	void ApplyLink() {
		bool up = manualUp && scheduleUp;
		if (up == linkUp) {
			return;
		}

		linkUp = up;
		toA.SetDown(!up);
		toB.SetDown(!up);
		if constexpr (Reports) {
			a.SetUp(up);
			b.SetUp(up);
		}
	}


	bool linkUp = true;
	bool manualUp = true;
	bool scheduleUp = true;
};


// A packet line, as StreamLine, for packets of up to MaxPacket bytes.
template<std::size_t MaxPacket, std::size_t Capacity, bool Reports = false>
class PacketLine {
public:
	explicit PacketLine(const Faults& faults) : toB(faults, faults.seed), toA(faults, faults.seed ^ 0x5A5A5A5Au), a(toB), b(toA),
		schedule(faults) {
	}


	// Advances the schedule to nowUs. The schedule changes only the schedule's own state: a line taken down by hand stays down.
	void SetNow(uint32 nowUs) {
		toB.SetNow(nowUs);
		toA.SetNow(nowUs);
		scheduleUp = schedule.UpAt(nowUs);
		ApplyLink();
	}


	// Takes the line down or up by hand. A line taken down by hand stays down until it is taken up by hand, and a line taken up is still
	// down while its schedule has it down. Setting the state the line has is no change.
	void SetLinkUp(bool up) {
		manualUp = up;
		ApplyLink();
	}


	template<typename Sink>
	void DeliverToB(Sink& sink) {
		toB.Deliver(sink);
	}


	template<typename Sink>
	void DeliverToA(Sink& sink) {
		toA.Deliver(sink);
	}


	PacketWire<Capacity, MaxPacket> toB;
	PacketWire<Capacity, MaxPacket> toA;
	PacketEnd<MaxPacket, Capacity, Reports> a;
	PacketEnd<MaxPacket, Capacity, Reports> b;
	OutageSchedule schedule;


private:
	// The line is up when it is up by hand and up by its schedule. A change of that is applied to both wires and, on a reporting line, to
	// both ends.
	void ApplyLink() {
		bool up = manualUp && scheduleUp;
		if (up == linkUp) {
			return;
		}

		linkUp = up;
		toA.SetDown(!up);
		toB.SetDown(!up);
		if constexpr (Reports) {
			a.SetUp(up);
			b.SetUp(up);
		}
	}


	bool linkUp = true;
	bool manualUp = true;
	bool scheduleUp = true;
};

}
