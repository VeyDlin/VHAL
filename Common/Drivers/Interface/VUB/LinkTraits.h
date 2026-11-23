#pragma once
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <cstddef>

namespace VUB {


// How a link starts a transfer. Either: both sides may. Self: this side clocks (an SPI master). Peer: the other side clocks (an SPI
// slave). Polled: a shared bus where a node sends when it is polled.
enum class Initiator : uint8 {
	Either,
	Self,
	Peer,
	Polled
};


// Whether a link carries both directions at once. Unset is an error: the link must say which it is.
enum class Duplex : uint8 {
	Unset,
	Half,
	Full
};


// The traits of a link, given at compile time. Each method sets one trait and returns the traits, so a link writes:
//   static constexpr LinkTraits traits = LinkTraits().Framed().MaxPacket(64).HalfDuplex();
class LinkTraits {
public:
	bool framed = false;
	bool reliable = false;
	bool reportsState = false;
	Duplex duplex = Duplex::Unset;
	Initiator starts = Initiator::Either;
	std::size_t maxPacket = 0;


	constexpr LinkTraits Framed() const {
		LinkTraits next = *this;
		next.framed = true;
		return next;
	}


	constexpr LinkTraits Reliable() const {
		LinkTraits next = *this;
		next.reliable = true;
		return next;
	}


	// The link tells the stack when it goes up and down (USB configured, BLE connected). Without it the link is always up.
	constexpr LinkTraits ReportsState() const {
		LinkTraits next = *this;
		next.reportsState = true;
		return next;
	}


	// The largest packet the link carries, in bytes. The packet is what the blocks build; framing bytes of a byte-stream link are not in it.
	constexpr LinkTraits MaxPacket(std::size_t bytes) const {
		Require<"the MaxPacket of a link is zero">(bytes > 0);

		LinkTraits next = *this;
		next.maxPacket = bytes;
		return next;
	}


	constexpr LinkTraits HalfDuplex() const {
		LinkTraits next = *this;
		next.duplex = Duplex::Half;
		return next;
	}


	constexpr LinkTraits FullDuplex() const {
		LinkTraits next = *this;
		next.duplex = Duplex::Full;
		return next;
	}


	constexpr LinkTraits Starts(Initiator value) const {
		LinkTraits next = *this;
		next.starts = value;
		return next;
	}
};

}
