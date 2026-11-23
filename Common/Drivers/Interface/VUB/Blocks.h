#pragma once
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>

namespace VUB {


// The kind of a block in a chain. The builder records it for each block.
enum class BlockKind : uint8 {
	link,
	framing,
	transform,
	layer
};


// The outcome of one byte pushed into a framing. pending: no packet is complete yet. packet: a whole packet is in the output, and its
// length is set. broken: the bytes were not a frame; they are dropped. tooLong: the frame does not fit in the output; it is dropped.
enum class FrameStatus : uint8 {
	pending,
	packet,
	broken,
	tooLong
};


// What a layer removes from a received packet. consumed is the number of bytes of its header, taken from the front. deliver is false
// when the layer kept the packet (an acknowledgement, a beacon), so the handler does not get it.
struct Taken {
	std::size_t consumed;
	bool deliver;
};


// The options of a block that takes none, and the placeholder of a block that is not in the chain.
struct None {
};


// The lane a packet waits in on the send side. Control packets (from the application) go before bulk packets, which the stack pulls
// from the layers' Next only when the link is free. A layer's own packet (from its Process or its outbox) is on the own lane: it goes
// before the control packets, and it goes while the peer is down. A chain whose layers do not report their peer keeps the own packets
// in the control lane.
enum class Lane : uint8 {
	control,
	own,
	bulk
};


// A link that takes bytes: Write returns how many bytes it accepted, which may be fewer than offered (zero when it is busy). The
// received bytes come in through the stack's Feed. A link has traits as well.
template<typename Link>
concept StreamLink = requires(
	Link& link,
	std::span<const uint8> bytes
) {
	typename Constant<Link::traits>;
	{ link.Write(bytes) } -> std::same_as<Result<std::size_t>>;
	requires std::same_as<std::remove_cvref_t<decltype(Link::traits)>, LinkTraits>;
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
concept PacketLink = requires(
	Link& link,
	std::span<const uint8> packet
) {
	typename Constant<Link::traits>;
	{ link.Send(packet) } -> std::same_as<ResultStatus>;
	requires std::same_as<std::remove_cvref_t<decltype(Link::traits)>, LinkTraits>;
};


template<typename Link>
concept LinkBlock = StreamLink<Link> || PacketLink<Link>;


// A link with ReportsState() in its traits. SetStateSink gives the link the sink to report into; the link reports its current state
// through the sink when it is given one. The stack gives a null sink when it is destroyed.
template<typename Link>
concept ReportingLink = requires(
	Link& link,
	StateSink sink
) {
	link.SetStateSink(sink);
};


// Finds packet boundaries in a byte stream. Frame writes the bytes of one packet with its boundaries. Push takes one received byte and
// reports, through FrameStatus, whether a packet is now complete. Growth(length) is the most bytes Frame adds to a packet of that length.
// usesTime says whether the framing reads the clock (the gap framing does), so a chain that has one needs a clock. usesTime and
// Growth are constant expressions: the builder reads them at compile time. An optional Reset() drops a partial frame; the stack calls it
// when the link changes state (up or down), if the framing has it.
//
// A framing with usesTime takes the time of each byte: Push(byte, nowUs, out, length), and Poll(nowUs, out, length) which the stack
// calls from Process, so a frame that ends in silence comes out without a byte after it. Ready(nowUs) says whether a frame may start
// on the wire (false while the gap after the last byte, received or sent, has not passed), and Sent(nowUs, count) tells the framing
// how many bytes the link took. The stack calls Ready before it frames a packet and Sent after each Write.
template<typename Framing>
concept PushesBytes = requires(
	Framing& framing,
	uint8 byte,
	std::span<uint8> out,
	std::size_t& length
) {
	{ framing.Push(byte, out, length) } -> std::same_as<FrameStatus>;
};


template<typename Framing>
concept TimedFraming = requires(
	Framing& framing,
	uint8 byte,
	uint32 nowUs,
	std::span<uint8> out,
	std::size_t& length
) {
	{ framing.Push(byte, nowUs, out, length) } -> std::same_as<FrameStatus>;
	{ framing.Poll(nowUs, out, length) } -> std::same_as<FrameStatus>;
	{ framing.Ready(nowUs) } -> std::convertible_to<bool>;
	{ framing.Sent(nowUs, std::size_t{}) } -> std::same_as<void>;
};


template<typename Framing>
concept FramingBlock = requires(
	Framing& framing,
	const Meta& meta,
	std::span<const uint8> packet,
	std::span<uint8> out
) {
	typename Constant<Framing::usesTime>;
	typename Constant<Framing::Growth(std::size_t{0})>;
	{ Framing::usesTime } -> std::convertible_to<bool>;
	{ Framing::Growth(std::size_t{}) } -> std::convertible_to<std::size_t>;
	{ framing.Frame(meta, packet, out) } -> std::same_as<Result<std::size_t>>;
} && ((Framing::usesTime && TimedFraming<Framing>) || (!Framing::usesTime && PushesBytes<Framing>));


// A block that changes a packet into another packet on send and back on receive. growth is the most bytes Encode adds, and is a
// constant expression. fingerprint is the block's part of the configuration fingerprint. Encode writes into out (at most length + growth
// bytes); Decode works in place and returns the length of the decoded packet. An error from Decode drops the packet and is counted.
template<typename Transform>
concept TransformBlock = requires(
	Transform& transform,
	const Meta& meta,
	std::span<const uint8> in,
	std::span<uint8> out,
	std::span<uint8> packet
) {
	typename Constant<Transform::growth>;
	{ Transform::growth } -> std::convertible_to<std::size_t>;
	{ Transform::fingerprint } -> std::convertible_to<uint32>;
	{ transform.Encode(meta, in, out) } -> std::same_as<Result<std::size_t>>;
	{ transform.Decode(meta, packet) } -> std::same_as<Result<std::size_t>>;
};


// A synchronous engine computes what a transform describes (a CRC, AES). Version 1 has synchronous engines only. Engine has no state of
// its own: the transform calls its static members.
template<typename Engine>
concept EngineBlock = requires {
	requires Engine::synchronous;
};


// An engine that can compute one algorithm. Algorithm is a type the transform gives, with the algorithm's parameters in
// Algorithm::parameters (a CRC is CrcAlgorithm<parameters>). Supports<Algorithm>() says whether the engine computes it, and
// Run<Algorithm>(data) computes it over the bytes. The builder refuses a transform whose engine does not support its algorithm: the
// transform checks it when the transform type is instantiated.
template<typename Engine, typename Algorithm>
concept EngineFor = EngineBlock<Engine> && requires(std::span<const uint8> data) {
	requires Engine::template Supports<Algorithm>();
	{ Engine::template Run<Algorithm>(data) };
};


// A block with state and time. header is the number of bytes Add puts in front of a packet. Add writes header + in into out. Take
// removes the header from a received packet, or keeps it. Process(now, outbox) runs once per Process call, and may send packets of its
// own through the outbox. makesBulk says the layer has bulk packets: then Next gives the next one, and the stack asks for it only when
// the link is free and no control packet waits. A layer that does not make bulk leaves no Next behind. header, usesTime and makesBulk
// are constant expressions. Two members are optional. OnLink(bool up, uint32 now) is called on each change of the connection, in chain
// order, from Process. A layer that declares reportsPeer = true may call outbox.SetPeer(up) from its Process to declare its peer down or
// up; the stack treats that as a change of the connection. A layer that declares passesUp = true may call outbox.Deliver(packet) from its
// Process to pass a packet up the receive path from its own position. A layer that declares usesLinkState = true may call outbox.LinkUp()
// from its Process to ask whether the physical link is up; on a link that does not report its state the answer is always true.
template<typename Layer>
concept LayerBlock = requires(
	Layer& layer,
	const Meta& meta,
	std::span<const uint8> in,
	std::span<uint8> out,
	std::span<const uint8> packet,
	const Outbox& outbox,
	uint32 nowUs
) {
	typename Constant<Layer::header>;
	typename Constant<Layer::usesTime>;
	typename Constant<Layer::makesBulk>;
	{ Layer::header } -> std::convertible_to<std::size_t>;
	{ Layer::fingerprint } -> std::convertible_to<uint32>;
	{ Layer::usesTime } -> std::convertible_to<bool>;
	{ Layer::makesBulk } -> std::convertible_to<bool>;
	{ layer.Add(meta, in, out) } -> std::same_as<Result<std::size_t>>;
	{ layer.Take(meta, packet) } -> std::same_as<Result<Taken>>;
	{ layer.Process(nowUs, outbox) } -> std::same_as<void>;
} && (!Layer::makesBulk || requires(
	Layer& bulkLayer,
	Meta& meta,
	std::span<uint8> out
) {
	{ bulkLayer.Next(meta, out) } -> std::same_as<Result<std::size_t>>;
});

}
