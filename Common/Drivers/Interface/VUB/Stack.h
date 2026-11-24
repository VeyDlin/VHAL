#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Builder.h>
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/LinkState.h>
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Drivers/Interface/VUB/Ring.h>
#include <Drivers/Interface/VUB/Storage.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>

namespace VUB {


// A stack built from a chain. Make is a captureless lambda that returns the Builder of the chain:
//
//   using Link = Stack<+[] {
//       return Builder()
//           .Layer<Requests>()
//           .Transform<Crc>()
//           .Framing<Cobs>()
//           .Link<UartLink>()
//           .MaxPayload(64)
//           .OnReceive<&HandlePacket>()
//           .Clock<&MicrosecondClock>();
//   }>;
//
// The stack is given its link object: Link link(uart); Link stack(link);
//
// Send encodes a packet at once, through the layers and transforms, into the control queue. Process reads the clock, takes the bytes
// and packets received, decodes them up the chain, runs every layer's Process, and then sends: the layers' own packets first, then the
// control packets, then the bulk packets of a layer's Next, while the link accepts, at most PerProcess packets a call. A packet the link
// refuses is kept for the next call; nothing is dropped for a busy link. A layer may pass a packet up from its Process, through its outbox
// (Deliver): the packet goes through the blocks above the layer, and on to the receive handler.
//
// Feed (a byte-stream link) and FeedPacket (a packet link) are called from the receive interrupt. Everything else runs in one context:
// Send, Process and Drops. Another task calling Send needs a lock of its own.
//
// How a link hands its data over. A link with SetReceiver gets a Receiver in its constructor, and feeds it from its interrupt; the stack
// takes it back in its destructor, so such a stack is not copyable. A link with Poll is polled by Process before the drain, and feeds the
// receiver what it reads, up to the receiver's Room. A link with neither is fed by the application through Feed or FeedPacket. A link with
// both is a compile error.
//
// The connection. A link with ReportsState() in its traits tells the stack up and down through the sink it is given; a layer that
// declares reportsPeer tells it the peer's state from Process. Each change is applied in Process, in this order: the framing's partial
// frame is reset, a down discards the bytes in the receive ring and the packets in flight, the layers' OnLink are called. The connection
// is up when the link is up and the peer is not declared down. While the link is down, Process offers nothing to the link. While only the
// peer is down, the layers' own packets still go out, and the application's packets and the bulk packets wait. A stack without either
// declaration is always connected and keeps no state.
//
// Sizes: the buffers are the packet size of the chain, computed at compile time. A block or a feature that the chain does not use
// has no member: the storage of a disabled part is an empty type, and its code is not instantiated.
// Whether the framing of a chain uses time. Has is false when the chain has no framing, and Entry is then not read.
template<bool Has, typename Entry>
struct FramingUsesTime {
	static constexpr bool value = false;
};


template<typename Entry>
struct FramingUsesTime<true, Entry> {
	static constexpr bool value = Entry::BlockType::usesTime;
};


template<auto (*Make)()>
class Stack {
public:
	using Builder = decltype(Make());
	using Tuple = typename Builder::Tuple;
	using LinkType = typename Builder::LinkType;


	static constexpr Builder built = Make();
	static constexpr std::size_t count = Builder::count;
	static constexpr bool checked = Builder::Check(built.settings);
	static_assert(checked, "the chain was not checked");


	// The options of entry Index of the chain, as a constant value.
	template<std::size_t Index>
	static constexpr auto OptionsAt = ListAt<Index>(built.entries).options;


	// The type stored for entry Index. A block that declares Configured is stored as that alias, with its options as constants; any other
	// block is stored as its Stored type and built from its options.
	template<std::size_t Index>
	using StoredAt = typename StoredOf<std::tuple_element_t<Index, Tuple>, OptionsAt<Index>>::type;


	template<std::size_t... Index>
	static std::tuple<StoredAt<Index>...> StorageOf(std::index_sequence<Index...>);


	using Storage = decltype(StorageOf(std::make_index_sequence<count>{}));


	static constexpr LinkTraits linkTraits = Builder::ChainLinkTraits();
	static constexpr bool streamLink = Builder::IsStreamLink();
	static constexpr bool linkReports = linkTraits.reportsState;
	static constexpr bool linkReceives = ReceivingLink<LinkType>;
	static constexpr bool linkPolled = PollingLink<LinkType>;
	static constexpr bool peersReport = Builder::PeersReport();
	static constexpr bool hasLinkState = linkReports || peersReport;
	static constexpr Down whileDown = built.settings.whileDown;
	static constexpr std::size_t packetBytes = Builder::PacketBound(built.settings);
	static constexpr std::size_t framedBytes = Builder::FramedBound(built.settings);
	static constexpr std::size_t framingIndex = Builder::FramingIndex();
	static constexpr bool hasFraming = framingIndex < count;
	using FramingEntry = std::tuple_element_t<(framingIndex < count ? framingIndex : 0), Tuple>;
	// A framing that uses time gets the time of each received byte, and the ring keeps it. Any other chain keeps no time.
	static constexpr bool framingTimed = streamLink && FramingUsesTime<hasFraming, FramingEntry>::value;
	static constexpr bool hasSteps = Builder::StepCount() > 0;
	static constexpr bool hasBulk = Builder::BulkCount() > 0;
	static constexpr std::size_t receiveBytes = linkTraits.maxPacket;
	static constexpr std::size_t receiveStart = streamLink ? (count >= 2 ? count - 2 : 0) : (count >= 1 ? count - 1 : 0);
	static constexpr std::size_t sendSlots = built.settings.sendQueue + 1;
	static constexpr std::size_t ringSlots = built.settings.receiveRing + 1;
	static constexpr std::size_t receiveSlots = built.settings.receiveQueue + 1;
	static constexpr std::size_t perProcess = built.settings.perProcess;
	static constexpr void (*receiveFn)(const Meta&, std::span<const uint8>) = built.settings.receiveFn;
	static constexpr uint32 (*clockFn)() = built.settings.clockFn;
	static constexpr bool hasClock = built.settings.hasClock;


	explicit Stack(LinkType& linkObject) : link(&linkObject), detach(link), receiverDetach(link), blocks(MakeBlocks(std::make_index_sequence<count>{})) {
		if constexpr (hasLinkState) {
			LinkRecord& record = linkRecord.Get();
			record.reportedUp = !linkReports;
			record.linkUp = !linkReports;
			record.connected = !linkReports;
		}

		if constexpr (linkReports) {
			link->SetStateSink(StateSink(this, &Stack::ReportLinkThunk));
		}

		if constexpr (linkReceives) {
			link->SetReceiver(MakeReceiver());
		}
	}


	// Encodes one payload and queues it. Returns ok when it is queued, busy when the control queue is full (the payload is not queued and
	// nothing is counted: the caller sends it again later), messageTooLong when it is larger than MaxPayload, noConnection while the
	// connection is down under the Fail option, or the status of a block that refused it.
	ResultStatus Send(std::span<const uint8> payload, const Meta& meta = Meta()) {
		if (payload.size() > built.settings.maxPayload) {
			return ResultStatus::messageTooLong;
		}

		if constexpr (hasLinkState) {
			if (whileDown == Down::Fail && !LinkUp()) {
				return ResultStatus::noConnection;
			}
		}

		if (QueueFull(control.indexes, sendSlots)) {
			return ResultStatus::busy;
		}

		return SendFrom<0>(payload, meta, Lane::control, true);
	}


	// The main loop call. Reads the clock, polls the link if it has Poll, applies the link's state, takes the received bytes and packets up
	// the chain, runs the layers' Process, and sends.
	void Process() {
		uint32 now = 0;
		if constexpr (hasClock) {
			now = clockFn();
		}

		if constexpr (linkPolled) {
			link->Poll(MakeReceiver());
		}

		if constexpr (hasLinkState) {
			SampleLink(now);
		}

		if constexpr (streamLink) {
			DrainRing(now);
		} else {
			DrainQueue();
		}

		ProcessLayers<0>(now);
		if constexpr (hasLinkState) {
			ApplyConnection(now, false, linkRecord.Get().linkUp);
		}

		Egress(now);
	}


	// Takes one received byte. Called from the receive interrupt of a byte-stream link. A full ring drops the byte and counts it. A
	// framing that uses time gets the time of the byte from the clock, here, so the byte keeps the time it arrived at.
	void Feed(uint8 byte) requires (streamLink) {
		if constexpr (framingTimed) {
			uint32 stamp = clockFn();
			if (!TimedRingPush(ring.indexes, ring.Span(), stamps.Span(), byte, stamp)) {
				CountIsr(ringFullIsr);
			}
		} else {
			if (!ByteRingPush(ring.indexes, ring.Span(), byte)) {
				CountIsr(ringFullIsr);
			}
		}
	}


	// The block of type Block in the chain, for the settings it changes at run time (a baud rate). The chain must hold one block of the
	// type: a chain with two of them is a compile error, since Find would have to pick one. Only a block that is in the chain has a member
	// here, so a chain without it has no code.
	template<typename Block>
	StoredElement<BlockIndexOf<Tuple, Block>(), Storage>& Find() {
		constexpr std::size_t index = BlockIndexOf<Tuple, Block>();
		static_assert(index < count, "the chain has no block of this type");
		static_assert(CountBlocksOf<Block>(std::make_index_sequence<count>{}) <= 1,
			"the chain has more than one block of this type: Find cannot tell them apart, so the chain holds each type once");
		return std::get<index>(blocks);
	}


	// Takes one received packet. Called from the receive interrupt of a packet link. A packet longer than the receive buffer counts as too
	// long; a packet that finds the queue full counts as queue full.
	void FeedPacket(std::span<const uint8> packet) requires (!streamLink) {
		ResultStatus status = QueuePush(rxQueue.indexes, rxQueue.View(), packet, 0);
		if (status == ResultStatus::messageTooLong) {
			CountIsr(tooLongIsr);
		} else if (status == ResultStatus::busy) {
			CountIsr(queueFullIsr);
		}
	}


	// A copy of the counters.
	DropCounters Drops() const {
		DropCounters snapshot;
		static_cast<DropCounts&>(snapshot) = drops;
		snapshot.ringFull = ringFullIsr;
		snapshot.queueFull = queueFullIsr;
		uint32 tooLongTotal = static_cast<uint32>(drops.tooLong) + tooLongIsr;
		snapshot.tooLong = tooLongTotal > 65535 ? 65535 : static_cast<uint16>(tooLongTotal);
		if constexpr (hasLinkState) {
			snapshot.linkDown = linkRecord.Get().dropped;
		}

		return snapshot;
	}


	// Whether the stack is connected: the link is up and no layer has declared the peer down, as of the last Process. Present only when the
	// link reports its state or a layer reports its peer.
	bool LinkUp() const requires (hasLinkState) {
		return linkRecord.Get().connected;
	}


	// How many times the connection went from up to down. Present only when LinkUp is.
	uint16 LinkDowns() const requires (hasLinkState) {
		return linkRecord.Get().downs;
	}


private:
	struct FramedState {
		uint16 length = 0;
		uint16 sent = 0;
		bool valid = false;
		bool own = false;
	};


	struct HeldState {
		uint16 length = 0;
		uint8 flags = 0;
		bool valid = false;
	};


	LinkType* link;
	[[no_unique_address]] Detach<linkReports, LinkType> detach;
	[[no_unique_address]] ReceiverDetach<linkReceives, LinkType> receiverDetach;
	[[no_unique_address]] Storage blocks;
	[[no_unique_address]] Buffer<hasSteps, packetBytes, 1> sendA;
	[[no_unique_address]] Buffer<hasSteps, packetBytes, 2> sendB;
	[[no_unique_address]] Buffer<hasBulk, packetBytes, 3> pulled;
	[[no_unique_address]] Buffer<hasBulk, packetBytes, 4> held;
	[[no_unique_address]] State<hasBulk, HeldState, 5> heldState;
	[[no_unique_address]] Buffer<streamLink && hasFraming, framedBytes, 6> framed;
	[[no_unique_address]] State<streamLink && hasFraming, FramedState, 7> framedState;
	[[no_unique_address]] QueueStorage<true, sendSlots, packetBytes, 8> control;
	[[no_unique_address]] QueueStorage<peersReport, sendSlots, packetBytes, 14> ownQueue;
	[[no_unique_address]] Buffer<true, receiveBytes, 9> rx;
	[[no_unique_address]] RingStorage<streamLink, ringSlots, 10> ring;
	[[no_unique_address]] StampStorage<framingTimed, ringSlots, 13> stamps;
	[[no_unique_address]] QueueStorage<!streamLink, receiveSlots, receiveBytes, 11> rxQueue;
	[[no_unique_address]] State<hasLinkState, LinkRecord, 12> linkRecord;
	DropCounts drops;
	volatile uint16 ringFullIsr = 0;
	volatile uint16 queueFullIsr = 0;
	volatile uint16 tooLongIsr = 0;


	// The step with index From is applied to packet and its result goes into a step buffer, alternating between the two buffers. The
	// packet reaches the end of the chain (the framing or the link) and is staged on the lane. A packet with a caller (caller true)
	// returns its failure to that caller and is not counted. A packet with no caller (a layer's own packet, a bulk packet) is counted
	// where it is refused: a layer's Add under layerError, a transform's Encode under transformError. A busy refusal is not counted: the
	// layer keeps the packet and sends it again.
	template<std::size_t From>
	ResultStatus SendFrom(std::span<const uint8> packet, const Meta& meta, Lane lane, bool caller) {
		if constexpr (From >= count) {
			return Stage(packet, meta, lane);
		} else {
			using Entry = std::tuple_element_t<From, Tuple>;
			if constexpr (Entry::kind == BlockKind::layer) {
				std::span<uint8> out = StepBuffer<From>();
				Result<std::size_t> added = std::get<From>(blocks).Add(meta, packet, out);
				if (added.IsErr()) {
					if (!caller && added.Error() != ResultStatus::busy) {
						CountDrop(drops.layerError);
					}

					return added.Error();
				}

				if (added.Value() > out.size()) {
					if (!caller) {
						CountDrop(drops.layerError);
					}

					return ResultStatus::messageTooLong;
				}

				return SendFrom<From + 1>(out.first(added.Value()), meta, lane, caller);
			} else if constexpr (Entry::kind == BlockKind::transform) {
				std::span<uint8> out = StepBuffer<From>();
				Result<std::size_t> encoded = std::get<From>(blocks).Encode(meta, packet, out);
				if (encoded.IsErr()) {
					if (!caller && encoded.Error() != ResultStatus::busy) {
						CountDrop(drops.transformError);
					}

					return encoded.Error();
				}

				if (encoded.Value() > out.size()) {
					if (!caller) {
						CountDrop(drops.transformError);
					}

					return ResultStatus::messageTooLong;
				}

				return SendFrom<From + 1>(out.first(encoded.Value()), meta, lane, caller);
			} else {
				return Stage(packet, meta, lane);
			}
		}
	}


	template<std::size_t From>
	std::span<uint8> StepBuffer() {
		if constexpr (From % 2 == 0) {
			return sendA.Span();
		} else {
			return sendB.Span();
		}
	}


	// Puts a finished packet on its lane: a queue, or the one bulk slot. The own lane is used only by a chain that has an own queue.
	ResultStatus Stage(std::span<const uint8> packet, const Meta& meta, Lane lane) {
		if constexpr (peersReport) {
			if (lane == Lane::own) {
				return QueuePush(ownQueue.indexes, ownQueue.View(), packet, meta.flags);
			}
		}

		if (lane == Lane::control) {
			return QueuePush(control.indexes, control.View(), packet, meta.flags);
		}

		if constexpr (hasBulk) {
			std::span<uint8> slot = held.Span();
			for (std::size_t index = 0; index < packet.size(); index++) {
				slot[index] = packet[index];
			}

			heldState.Get().length = static_cast<uint16>(packet.size());
			heldState.Get().flags = meta.flags;
			heldState.Get().valid = true;
			return ResultStatus::ok;
		} else {
			return ResultStatus::notSupported;
		}
	}


	// Runs the blocks below the chain's top on a received packet, bottom first. Layers and transforms: a decode error drops the packet
	// and counts it; a layer that keeps the packet ends the walk.
	template<std::size_t Below>
	ResultStatus ReceiveBelow(std::span<uint8> packet) {
		if constexpr (Below == 0) {
			Meta meta{};
			receiveFn(meta, packet);
			return ResultStatus::ok;
		} else {
			using Entry = std::tuple_element_t<Below - 1, Tuple>;
			Meta meta{};
			if constexpr (Entry::kind == BlockKind::transform) {
				Result<std::size_t> decoded = std::get<Below - 1>(blocks).Decode(meta, packet);
				if (decoded.IsErr()) {
					CountDrop(drops.transformError);
					return decoded.Error();
				}

				if (decoded.Value() > packet.size()) {
					CountDrop(drops.transformError);
					return ResultStatus::invalidData;
				}

				return ReceiveBelow<Below - 1>(packet.first(decoded.Value()));
			} else if constexpr (Entry::kind == BlockKind::layer) {
				Result<Taken> taken = std::get<Below - 1>(blocks).Take(meta, packet);
				if (taken.IsErr()) {
					CountDrop(drops.layerError);
					return taken.Error();
				}

				if (!taken.Value().deliver) {
					return ResultStatus::ok;
				}

				if (taken.Value().consumed > packet.size()) {
					CountDrop(drops.layerError);
					return ResultStatus::invalidData;
				}

				return ReceiveBelow<Below - 1>(packet.subspan(taken.Value().consumed));
			} else {
				return ReceiveBelow<Below - 1>(packet);
			}
		}
	}


	void ReceivePacket(std::span<uint8> packet) {
		(void)ReceiveBelow<receiveStart>(packet);
	}


	// Takes the received bytes out of the ring, through the framing. A byte-stream link only. A framing that uses time gets each byte with
	// its time, and is then polled with the time of this call, so a frame that ends in silence comes out.
	void DrainRing(uint32 now) {
		if constexpr (streamLink) {
			uint8 value = 0;
			if constexpr (framingTimed) {
				uint32 stamp = 0;
				while (TimedRingPop(ring.indexes, ring.Span(), stamps.Span(), value, stamp)) {
					std::size_t length = 0;
					FrameStatus state = std::get<framingIndex>(blocks).Push(value, stamp, rx.Span(), length);
					HandleFrame(state, length);
				}

				std::size_t length = 0;
				FrameStatus state = std::get<framingIndex>(blocks).Poll(now, rx.Span(), length);
				HandleFrame(state, length);
			} else {
				while (ByteRingPop(ring.indexes, ring.Span(), value)) {
					std::size_t length = 0;
					FrameStatus state = std::get<framingIndex>(blocks).Push(value, rx.Span(), length);
					HandleFrame(state, length);
				}
			}
		}
	}


	// Acts on what the framing reports for a byte: a packet goes up the chain, a broken frame and a frame too long are counted.
	void HandleFrame(FrameStatus state, std::size_t length) {
		switch (state) {
			case FrameStatus::packet:
				ReceivePacket(rx.Span().first(length));
				break;
			case FrameStatus::broken:
				CountDrop(drops.framingError);
				break;
			case FrameStatus::tooLong:
				CountDrop(drops.tooLong);
				break;
			case FrameStatus::pending:
				break;
		}
	}


	// The stored blocks, built in chain order. A block that declares Configured is default-constructed, since its options are in its type.
	// Any other block is built from its options, which are read from the chain value, so a configured block never reads the chain.
	template<std::size_t... Index>
	static Storage MakeBlocks(std::index_sequence<Index...>) {
		return Storage(MakeStored<Index>()...);
	}


	template<std::size_t Index>
	static StoredAt<Index> MakeStored() {
		using Entry = std::tuple_element_t<Index, Tuple>;
		if constexpr (Entry::kind == BlockKind::link) {
			return None{};
		} else if constexpr (ConfiguredEntry<Entry, OptionsAt<Index>>) {
			return StoredAt<Index>{};
		} else if constexpr (std::is_same_v<typename Entry::OptionsType, None>) {
			return typename Entry::BlockType{};
		} else {
			return typename Entry::BlockType(ListAt<Index>(built.entries).options);
		}
	}


	// The number of blocks of type Block in the chain.
	template<typename Block, std::size_t... Index>
	static constexpr std::size_t CountBlocksOf(std::index_sequence<Index...>) {
		return (0 + ... + (std::is_same_v<typename std::tuple_element_t<Index, Tuple>::BlockType, Block> ? 1 : 0));
	}


	// Takes the received packets out of the queue. A packet link only.
	void DrainQueue() {
		if constexpr (!streamLink) {
			std::span<const uint8> packet;
			uint8 flags = 0;
			while (QueueFront(rxQueue.indexes, rxQueue.View(), packet, flags)) {
				std::size_t length = packet.size();
				std::span<uint8> slot = rx.Span();
				for (std::size_t index = 0; index < length; index++) {
					slot[index] = packet[index];
				}

				QueuePop(rxQueue.indexes, receiveSlots);
				ReceivePacket(slot.first(length));
			}
		}
	}


	using PeerFunction = void (*)(void*, bool);
	using DeliverFunction = ResultStatus (*)(void*, std::span<const uint8>);
	using LinkFunction = bool (*)(void*);


	template<std::size_t Index>
	void ProcessLayers(uint32 now) {
		if constexpr (Index < count) {
			using Entry = std::tuple_element_t<Index, Tuple>;
			if constexpr (Entry::kind == BlockKind::layer) {
				Outbox outbox(this, &Stack::template SendThunk<Index + 1>, PeerFor<Index>(), DeliverFor<Index>(), LinkFor<Index>());
				std::get<Index>(blocks).Process(now, outbox);
			}

			ProcessLayers<Index + 1>(now);
		}
	}


	// The peer function of a layer. A layer that does not declare its peer gets none, so no code for one is made.
	template<std::size_t Index>
	static PeerFunction PeerFor() {
		using Entry = std::tuple_element_t<Index, Tuple>;
		if constexpr (Entry::ReportsPeer()) {
			return &Stack::PeerThunk;
		} else {
			return nullptr;
		}
	}


	// The deliver function of a layer. A layer that does not declare passesUp gets none, so no code for one is made.
	template<std::size_t Index>
	static DeliverFunction DeliverFor() {
		using Entry = std::tuple_element_t<Index, Tuple>;
		if constexpr (Entry::PassesUp()) {
			return &Stack::template DeliverThunk<Index>;
		} else {
			return nullptr;
		}
	}


	// The link query of a layer. Only a layer that declares usesLinkState on a link that reports its state gets one; any other layer gets
	// none, and its outbox answers that the link is up.
	template<std::size_t Index>
	static LinkFunction LinkFor() {
		using Entry = std::tuple_element_t<Index, Tuple>;
		if constexpr (linkReports && Entry::UsesLinkState()) {
			return &Stack::LinkUpThunk;
		} else {
			return nullptr;
		}
	}


	// The deliver function of the layer at Index. The packet is copied into the receive buffer, since the blocks above decode in place. The
	// receive buffer is free in Process: the bytes and packets were taken before the layers ran. The walk starts at the block just above
	// the layer, ReceiveBelow<Index>, and runs to the handler, as it does for a packet the layer took.
	template<std::size_t Index>
	static ResultStatus DeliverThunk(void* owner, std::span<const uint8> packet) {
		Stack* self = static_cast<Stack*>(owner);
		if (packet.size() > receiveBytes) {
			CountDrop(self->drops.layerError);
			return ResultStatus::messageTooLong;
		}

		std::span<uint8> slot = self->rx.Span();
		for (std::size_t index = 0; index < packet.size(); index++) {
			slot[index] = packet[index];
		}

		return self->template ReceiveBelow<Index>(slot.first(packet.size()));
	}


	// The outbox of a layer. The packet is at the layer's level, so it starts at the step after the layer. A packet with no caller: one
	// the layer could not send is counted, and busy is not, since the layer keeps the packet and sends it again. Under Fail, a layer's
	// packet is refused only while the link is down: a down peer does not stop it.
	template<std::size_t From>
	static ResultStatus SendThunk(void* owner, std::span<const uint8> packet, const Meta& meta) {
		Stack* self = static_cast<Stack*>(owner);
		if (packet.size() > packetBytes) {
			CountDrop(self->drops.layerError);
			return ResultStatus::messageTooLong;
		}

		if constexpr (hasLinkState) {
			if (whileDown == Down::Fail && !self->linkRecord.Get().linkUp) {
				return ResultStatus::noConnection;
			}
		}

		if constexpr (peersReport) {
			if (QueueFull(self->ownQueue.indexes, sendSlots)) {
				return ResultStatus::busy;
			}

			return self->template SendFrom<From>(packet, meta, Lane::own, false);
		} else {
			if (QueueFull(self->control.indexes, sendSlots)) {
				return ResultStatus::busy;
			}

			return self->template SendFrom<From>(packet, meta, Lane::control, false);
		}
	}


	// The peer's state, as a layer declares it from its Process. The change is applied after the layers have run.
	static void PeerThunk(void* owner, bool up) {
		Stack* self = static_cast<Stack*>(owner);
		self->linkRecord.Get().peerUp = up;
	}


	// The link's state, as the link reports it. It only records the state; Process acts on it.
	static void ReportLinkThunk(void* owner, bool up) {
		Stack* self = static_cast<Stack*>(owner);
		self->linkRecord.Get().Report(up);
	}


	// The physical link's state as of the last Process: the state the link reports, not the connection.
	static bool LinkUpThunk(void* owner) {
		Stack* self = static_cast<Stack*>(owner);
		return self->linkRecord.Get().linkUp;
	}


	// The receiver of this stack, for a link that receives in an interrupt or is polled. Its functions are the stack's Feed and FeedPacket
	// and its room, so a byte or a packet that does not fit counts as it does for the application.
	Receiver MakeReceiver() {
		if constexpr (streamLink) {
			return Receiver(this, &Stack::ReceiveBytesThunk, nullptr, &Stack::RoomThunk);
		} else {
			return Receiver(this, nullptr, &Stack::ReceivePacketThunk, &Stack::RoomThunk);
		}
	}


	// Feeds the bytes of a byte-stream link through Feed, one by one.
	static void ReceiveBytesThunk(void* owner, std::span<const uint8> bytes) {
		Stack* self = static_cast<Stack*>(owner);
		for (uint8 value : bytes) {
			self->Feed(value);
		}
	}


	// Feeds one packet of a packet link through FeedPacket.
	static void ReceivePacketThunk(void* owner, std::span<const uint8> packet) {
		Stack* self = static_cast<Stack*>(owner);
		self->FeedPacket(packet);
	}


	// The room of the receive ring (a byte-stream link) or of the receive queue (a packet link).
	static std::size_t RoomThunk(void* owner) {
		Stack* self = static_cast<Stack*>(owner);
		if constexpr (streamLink) {
			return SlotsFree(self->ring.indexes, ringSlots);
		} else {
			return SlotsFree(self->rxQueue.indexes, receiveSlots);
		}
	}


	// Reads what the link reported since the last Process: whether it fell, and the state it reports. The falls are read first. The report
	// writes the state before it counts a fall, so a state read after the count is at least as new as the fall. A fall reported after the
	// count is read is seen by the next Process.
	void SampleLink(uint32 now) {
		LinkRecord& record = linkRecord.Get();
		uint16 falls = record.reportedFalls;
		bool fallen = falls != record.seenFalls;
		record.seenFalls = falls;
		record.linkUp = record.reportedUp;
		ApplyConnection(now, fallen, record.linkUp);
	}


	// Applies the connection: the link's state and the peer's. A fall of the link is a down even when the link came back up before this
	// Process. A fall while the stack is already down is not a down: the framing and the receive ring are left as they are. A change in
	// either direction resets the framing. On a down the queued packets and the receive ring are dropped as DropOnDown says. The layers are
	// told of each change, in chain order.
	void ApplyConnection(uint32 now, bool fallen, bool linkUpNow) {
		LinkRecord& record = linkRecord.Get();
		bool next = linkUpNow && record.peerUp;
		bool wentDown = record.connected && (fallen || !next);
		if (!wentDown && next == record.connected) {
			return;
		}

		if (wentDown) {
			record.connected = false;
			CountDrop(record.downs);
			DropOnDown();
			ResetFraming();
			TellLayers<0>(false, now);
		}

		if (next && !record.connected) {
			record.connected = true;
			ResetFraming();
			TellLayers<0>(true, now);
		}
	}


	// On a down. The receive ring's bytes are dropped: a partial frame is useless after a disconnect. The frame being written starts again
	// from its first byte under Wait, and is dropped under Fail; a layer's own frame under a peer-only down is kept, since its packets
	// still go out. The packets queued for the link wait under Wait, and are dropped under Fail. A layer's own packets are kept while the
	// link is up. The packets the receive queue holds are kept: they are whole and still go up.
	void DropOnDown() {
		LinkRecord& record = linkRecord.Get();
		if constexpr (streamLink) {
			AddDrops(record.dropped, ByteRingClear(ring.indexes, ringSlots));
		}

		if constexpr (streamLink && hasFraming) {
			FramedState& state = framedState.Get();
			if (state.valid) {
				bool dropFrame = whileDown == Down::Fail;
				if constexpr (peersReport) {
					dropFrame = dropFrame && (!state.own || !record.linkUp);
				}

				if (dropFrame) {
					state.valid = false;
					AddDrops(record.dropped, 1);
				} else {
					state.sent = 0;
				}
			}
		}

		if (whileDown == Down::Fail) {
			DropQueued(control.indexes, control.View(), sendSlots);
			if constexpr (peersReport) {
				if (!record.linkUp) {
					DropQueued(ownQueue.indexes, ownQueue.View(), sendSlots);
				}
			}

			if constexpr (hasBulk) {
				if (heldState.Get().valid) {
					heldState.Get().valid = false;
					AddDrops(record.dropped, 1);
				}
			}
		}
	}


	// Drops every packet a queue holds, and counts each one as dropped.
	void DropQueued(Indexes& indexes, const QueueView& view, std::size_t slotCount) {
		std::span<const uint8> packet;
		uint8 flags = 0;
		while (QueueFront(indexes, view, packet, flags)) {
			QueuePop(indexes, slotCount);
			AddDrops(linkRecord.Get().dropped, 1);
		}
	}


	// Resets the framing's partial frame, when the framing has a Reset.
	void ResetFraming() {
		if constexpr (streamLink && hasFraming) {
			using FramingType = std::remove_reference_t<decltype(std::get<framingIndex>(blocks))>;
			if constexpr (requires(FramingType& framing) { framing.Reset(); }) {
				std::get<framingIndex>(blocks).Reset();
			}
		}
	}


	// Tells each layer that has an OnLink, in chain order, that the connection is up or down.
	template<std::size_t Index>
	void TellLayers(bool up, uint32 now) {
		if constexpr (Index < count) {
			using Entry = std::tuple_element_t<Index, Tuple>;
			if constexpr (Entry::kind == BlockKind::layer) {
				using LayerType = typename Entry::BlockType;
				if constexpr (requires(LayerType& layer, bool value, uint32 time) { layer.OnLink(value, time); }) {
					std::get<Index>(blocks).OnLink(up, now);
				}
			}

			TellLayers<Index + 1>(up, now);
		}
	}


	// Gives one packet to the link. On a byte-stream link the packet is framed into the framed buffer, and WriteFramed sends it; own says
	// whether the packet is a layer's own, which may go on while the peer is down. A framing that uses time may first ask to wait for the
	// gap: then the packet stays where it is, and the call returns false. Returns false too when a packet link is busy.
	bool Offer(std::span<const uint8> packet, uint8 flags, uint32 now, [[maybe_unused]] bool own) {
		if constexpr (streamLink) {
			if constexpr (hasFraming) {
				if constexpr (framingTimed) {
					if (!std::get<framingIndex>(blocks).Ready(now)) {
						return false;
					}
				}

				Meta meta{};
				meta.flags = flags;
				Result<std::size_t> length = std::get<framingIndex>(blocks).Frame(meta, packet, framed.Span());
				if (length.IsErr()) {
					CountDrop(drops.sendError);
					return true;
				}

				framedState.Get().length = static_cast<uint16>(length.Value());
				framedState.Get().sent = 0;
				framedState.Get().valid = true;
				if constexpr (peersReport) {
					framedState.Get().own = own;
				}
			}

			return true;
		} else {
			ResultStatus status = link->Send(packet);
			if (status == ResultStatus::busy) {
				return false;
			}

			if (status != ResultStatus::ok) {
				CountDrop(drops.sendError);
			}

			return true;
		}
	}


	// Writes the rest of the framed packet. Returns true when the packet is complete, false when the link took less than the rest. A framing
	// that uses time is told how many bytes the link took, so it can keep the gap after them.
	bool WriteFramed(uint32 now) {
		if constexpr (streamLink && hasFraming) {
			FramedState& state = framedState.Get();
			std::span<const uint8> rest = std::span<const uint8>(framed.Span()).subspan(state.sent, state.length - state.sent);
			Result<std::size_t> written = link->Write(rest);
			if (written.IsErr()) {
				CountDrop(drops.sendError);
				state.valid = false;
				return true;
			}

			std::size_t taken = written.Value() < rest.size() ? written.Value() : rest.size();
			if constexpr (framingTimed) {
				std::get<framingIndex>(blocks).Sent(now, taken);
			}

			state.sent = static_cast<uint16>(state.sent + taken);
			if (state.sent < state.length) {
				return false;
			}

			state.valid = false;
			return true;
		} else {
			return true;
		}
	}


	// Asks the layers in chain order for their next bulk packet, at most room of them. A packet that is staged stays in the held slot,
	// and the call returns 0 for it. The call returns the number of packets it dropped (each one counted by SendFrom or here).
	std::size_t PullBulk(std::size_t room) {
		return PullFrom<0>(room);
	}


	template<std::size_t Index>
	std::size_t PullFrom(std::size_t room) {
		if constexpr (Index >= count) {
			return 0;
		} else {
			using Entry = std::tuple_element_t<Index, Tuple>;
			if constexpr (Entry::kind == BlockKind::layer && Entry::MakesBulk()) {
				Meta meta{};
				std::span<uint8> out = pulled.Span();
				Result<std::size_t> produced = std::get<Index>(blocks).Next(meta, out);
				if (produced.IsOk()) {
					if (produced.Value() > out.size()) {
						CountDrop(drops.layerError);
					} else if (SendFrom<Index + 1>(out.first(produced.Value()), meta, Lane::bulk, false) == ResultStatus::ok) {
						return 0;
					}

					// The packet was dropped and counted. The layers below may still have data in this call, within the room left.
					return room > 1 ? 1 + PullFrom<Index + 1>(room - 1) : 1;
				}

				if (produced.Error() != ResultStatus::noDataAvailable) {
					CountDrop(drops.layerError);
				}
			}

			return PullFrom<Index + 1>(room);
		}
	}


	// Sends from the queues while the link accepts: the layers' own packets, then the control packets, then the held bulk packet, then one
	// bulk packet from a layer. Every packet taken from a queue or a layer counts toward PerProcess, whether the link took it or
	// refused it, so one call stops within PerProcess packets. Nothing is offered while the link is down. While only the peer is down, the
	// layers' own packets go, and the control and bulk packets wait. A framed packet that is partly written goes on only when it is a
	// layer's own while the peer is down.
	void Egress(uint32 now) {
		std::size_t sentPackets = 0;
		while (sentPackets < perProcess) {
			// Without a peer report the connection is the link, so one state is read.
			bool connected = true;
			if constexpr (peersReport) {
				if (!linkRecord.Get().linkUp) {
					return;
				}

				connected = linkRecord.Get().connected;
			} else if constexpr (hasLinkState) {
				if (!linkRecord.Get().connected) {
					return;
				}
			}

			if constexpr (streamLink && hasFraming) {
				if (framedState.Get().valid) {
					if (!connected && !framedState.Get().own) {
						return;
					}

					if (!WriteFramed(now)) {
						return;
					}

					continue;
				}
			}

			std::span<const uint8> packet;
			uint8 flags = 0;
			if constexpr (peersReport) {
				if (QueueFront(ownQueue.indexes, ownQueue.View(), packet, flags)) {
					if (!Offer(packet, flags, now, true)) {
						return;
					}

					QueuePop(ownQueue.indexes, sendSlots);
					sentPackets++;
					continue;
				}
			}

			if (!connected) {
				return;
			}

			if (QueueFront(control.indexes, control.View(), packet, flags)) {
				if (!Offer(packet, flags, now, false)) {
					return;
				}

				QueuePop(control.indexes, sendSlots);
				sentPackets++;
				continue;
			}

			if constexpr (hasBulk) {
				if (heldState.Get().valid) {
					std::span<const uint8> bulk(held.Span().data(), heldState.Get().length);
					if (!Offer(bulk, heldState.Get().flags, now, false)) {
						return;
					}

					heldState.Get().valid = false;
					sentPackets++;
					continue;
				}

				std::size_t dropped = PullBulk(perProcess - sentPackets);
				if (dropped == 0 && !heldState.Get().valid) {
					return;
				}

				sentPackets += dropped;
			} else {
				return;
			}
		}
	}
};

}
