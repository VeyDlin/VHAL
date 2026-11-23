#pragma once
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Utilities/DataTypes.h>
#include <cstddef>


// How a link starts a transfer. Either: both sides may. Self: this side clocks (an SPI master). Peer: the other side clocks (an SPI
// slave). Polled: a shared bus where a node sends when it is polled.
enum class VubStarts : uint8 {
	Either,
	Self,
	Peer,
	Polled
};


// Whether a link carries both directions at once. Unset is an error: the link must say which it is.
enum class VubDuplex : uint8 {
	Unset,
	Half,
	Full
};


// The traits of a link, given at compile time. Each method sets one trait and returns the traits, so a link writes:
//   static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).HalfDuplex();
class VubLinkTraits {
public:
	bool framed = false;
	bool reliable = false;
	bool reportsState = false;
	VubDuplex duplex = VubDuplex::Unset;
	VubStarts starts = VubStarts::Either;
	std::size_t maxPacket = 0;


	constexpr VubLinkTraits Framed() const {
		VubLinkTraits next = *this;
		next.framed = true;
		return next;
	}


	constexpr VubLinkTraits Reliable() const {
		VubLinkTraits next = *this;
		next.reliable = true;
		return next;
	}


	// The link tells the stack when it goes up and down (USB configured, BLE connected). Without it the link is always up.
	constexpr VubLinkTraits ReportsState() const {
		VubLinkTraits next = *this;
		next.reportsState = true;
		return next;
	}


	// The largest packet the link carries, in bytes. The packet is what the blocks build; framing bytes of a byte-stream link are not in it.
	constexpr VubLinkTraits MaxPacket(std::size_t bytes) const {
		VubRequire<"the MaxPacket of a link is zero">(bytes > 0);

		VubLinkTraits next = *this;
		next.maxPacket = bytes;
		return next;
	}


	constexpr VubLinkTraits HalfDuplex() const {
		VubLinkTraits next = *this;
		next.duplex = VubDuplex::Half;
		return next;
	}


	constexpr VubLinkTraits FullDuplex() const {
		VubLinkTraits next = *this;
		next.duplex = VubDuplex::Full;
		return next;
	}


	constexpr VubLinkTraits Starts(VubStarts value) const {
		VubLinkTraits next = *this;
		next.starts = value;
		return next;
	}
};
