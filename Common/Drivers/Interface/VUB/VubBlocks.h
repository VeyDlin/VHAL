#pragma once
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>


// The kind of a block in a chain. The builder records it for each block.
enum class VubKind : uint8 {
	link,
	framing,
	transform,
	layer
};


// The outcome of one byte pushed into a framing. pending: no packet is complete yet. packet: a whole packet is in the output, and its
// length is set. broken: the bytes were not a frame; they are dropped. tooLong: the frame does not fit in the output; it is dropped.
enum class VubFrame : uint8 {
	pending,
	packet,
	broken,
	tooLong
};


// What a layer removes from a received packet. consumed is the number of bytes of its header, taken from the front. deliver is false
// when the layer kept the packet (an acknowledgement, a beacon), so the handler does not get it.
struct VubTake {
	std::size_t consumed;
	bool deliver;
};


// The options of a block that takes none, and the placeholder of a block that is not in the chain.
struct VubNone {
};


// The lane a packet waits in on the send side. Control packets (from the application and from the layers' Process) go before bulk
// packets, which the stack pulls from the layers' Next only when the link is free.
enum class VubLane : uint8 {
	control,
	bulk
};


// A link that takes bytes: Write returns how many bytes it accepted, which may be fewer than offered (zero when it is busy). The
// received bytes come in through the stack's Feed. A link has traits as well.
template<typename Link>
concept VubStreamLink = requires(
	Link& link,
	std::span<const uint8> bytes
) {
	typename VubConstant<Link::traits>;
	{ link.Write(bytes) } -> std::same_as<Result<std::size_t>>;
	requires std::same_as<std::remove_cvref_t<decltype(Link::traits)>, VubLinkTraits>;
} && !requires(
	Link& link,
	std::span<const uint8> packet
) {
	link.Send(packet);
};


// A link that takes whole packets: Send returns ok when it took the packet, busy when it cannot take it now (the stack keeps the
// packet and tries again), or another status for an error (the packet is then dropped and counted). Received packets come in through
// the stack's FeedPacket.
template<typename Link>
concept VubPacketLink = requires(
	Link& link,
	std::span<const uint8> packet
) {
	typename VubConstant<Link::traits>;
	{ link.Send(packet) } -> std::same_as<ResultStatus>;
	requires std::same_as<std::remove_cvref_t<decltype(Link::traits)>, VubLinkTraits>;
};


template<typename Link>
concept VubLink = VubStreamLink<Link> || VubPacketLink<Link>;


// Finds packet boundaries in a byte stream. Frame writes the bytes of one packet with its boundaries. Push takes one received byte and
// reports, through VubFrame, whether a packet is now complete. Growth(length) is the most bytes Frame adds to a packet of that length.
// usesTime says whether the framing reads the clock (the gap framing does), so a chain that has one needs a clock. usesTime and
// Growth are constant expressions: the builder reads them at compile time.
template<typename Framing>
concept VubFraming = requires(
	Framing& framing,
	const VubMeta& meta,
	std::span<const uint8> packet,
	std::span<uint8> out,
	uint8 byte,
	std::size_t& length
) {
	typename VubConstant<Framing::usesTime>;
	typename VubConstant<Framing::Growth(std::size_t{0})>;
	{ Framing::usesTime } -> std::convertible_to<bool>;
	{ Framing::Growth(std::size_t{}) } -> std::convertible_to<std::size_t>;
	{ framing.Frame(meta, packet, out) } -> std::same_as<Result<std::size_t>>;
	{ framing.Push(byte, out, length) } -> std::same_as<VubFrame>;
};


// A block that changes a packet into another packet on send and back on receive. growth is the most bytes Encode adds, and is a
// constant expression. fingerprint is the block's part of the configuration fingerprint. Encode writes into out (at most length + growth
// bytes); Decode works in place and returns the length of the decoded packet. An error from Decode drops the packet and is counted.
template<typename Transform>
concept VubTransform = requires(
	Transform& transform,
	const VubMeta& meta,
	std::span<const uint8> in,
	std::span<uint8> out,
	std::span<uint8> packet
) {
	typename VubConstant<Transform::growth>;
	{ Transform::growth } -> std::convertible_to<std::size_t>;
	{ Transform::fingerprint } -> std::convertible_to<uint32>;
	{ transform.Encode(meta, in, out) } -> std::same_as<Result<std::size_t>>;
	{ transform.Decode(meta, packet) } -> std::same_as<Result<std::size_t>>;
};


// A synchronous engine computes what a transform describes (a CRC, AES). Version 1 has synchronous engines only; the call shape of an
// engine is fixed with the first engine that uses it.
template<typename Engine>
concept VubEngine = requires {
	requires Engine::synchronous;
};


// A block with state and time. header is the number of bytes Add puts in front of a packet. Add writes header + in into out. Take
// removes the header from a received packet, or keeps it. Process(now, outbox) runs once per Process call, and may send packets of its
// own through the outbox. makesBulk says the layer has bulk packets: then Next gives the next one, and the stack asks for it only when
// the link is free and no control packet waits. A layer that does not make bulk leaves no Next behind. header, usesTime and makesBulk
// are constant expressions.
template<typename Layer>
concept VubLayer = requires(
	Layer& layer,
	const VubMeta& meta,
	std::span<const uint8> in,
	std::span<uint8> out,
	std::span<const uint8> packet,
	const VubOutbox& outbox,
	uint32 nowUs
) {
	typename VubConstant<Layer::header>;
	typename VubConstant<Layer::usesTime>;
	typename VubConstant<Layer::makesBulk>;
	{ Layer::header } -> std::convertible_to<std::size_t>;
	{ Layer::fingerprint } -> std::convertible_to<uint32>;
	{ Layer::usesTime } -> std::convertible_to<bool>;
	{ Layer::makesBulk } -> std::convertible_to<bool>;
	{ layer.Add(meta, in, out) } -> std::same_as<Result<std::size_t>>;
	{ layer.Take(meta, packet) } -> std::same_as<Result<VubTake>>;
	{ layer.Process(nowUs, outbox) } -> std::same_as<void>;
} && (!Layer::makesBulk || requires(
	Layer& bulkLayer,
	VubMeta& meta,
	std::span<uint8> out
) {
	{ bulkLayer.Next(meta, out) } -> std::same_as<Result<std::size_t>>;
});
