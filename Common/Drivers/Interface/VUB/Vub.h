#pragma once
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubBuilder.h>
#include <Drivers/Interface/VUB/VubDrops.h>
#include <Drivers/Interface/VUB/VubLinkTraits.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Drivers/Interface/VUB/VubOutbox.h>
#include <Drivers/Interface/VUB/VubRequire.h>
#include <Drivers/Interface/VUB/VubRing.h>
#include <Drivers/Interface/VUB/VubStorage.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <span>
#include <tuple>


// A stack built from a chain. Make is a captureless lambda that returns the VubBuilder of the chain:
//
//   using Link = Vub<+[] {
//       return VubBuilder()
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
// and packets received, decodes them up the chain, runs every layer's Process, and then sends: control packets first, then the bulk
// packets of a layer's Next, while the link accepts, at most PerProcess packets a call. A packet the link refuses is kept for the next
// call; nothing is dropped for a busy link.
//
// Feed (a byte-stream link) and FeedPacket (a packet link) are called from the receive interrupt. Everything else runs in one context:
// Send, Process and Drops. Another task calling Send needs a lock of its own.
//
// Sizes: the buffers are the packet size of the chain, computed at compile time. A block or a feature that the chain does not use
// has no member: the storage of a disabled part is an empty type, and its code is not instantiated.
template<auto (*Make)()>
class Vub {
public:
	using Builder = decltype(Make());
	using Tuple = typename Builder::Tuple;
	using LinkType = typename Builder::LinkType;
	using Storage = typename Builder::Storage;


	static constexpr Builder built = Make();
	static constexpr std::size_t count = Builder::count;
	static constexpr bool checked = Builder::Check(built.settings);
	static_assert(checked, "the chain was not checked");


	static constexpr VubLinkTraits linkTraits = Builder::LinkTraits();
	static constexpr bool streamLink = Builder::IsStreamLink();
	static constexpr std::size_t packetBytes = Builder::PacketBound(built.settings);
	static constexpr std::size_t framedBytes = Builder::FramedBound(built.settings);
	static constexpr std::size_t framingIndex = Builder::FramingIndex();
	static constexpr bool hasFraming = framingIndex < count;
	static constexpr bool hasSteps = Builder::StepCount() > 0;
	static constexpr bool hasBulk = Builder::BulkCount() > 0;
	static constexpr std::size_t receiveBytes = linkTraits.maxPacket;
	static constexpr std::size_t receiveStart = streamLink ? (count >= 2 ? count - 2 : 0) : (count >= 1 ? count - 1 : 0);
	static constexpr std::size_t sendSlots = built.settings.sendQueue + 1;
	static constexpr std::size_t ringSlots = built.settings.receiveRing + 1;
	static constexpr std::size_t receiveSlots = built.settings.receiveQueue + 1;
	static constexpr std::size_t perProcess = built.settings.perProcess;
	static constexpr void (*receiveFn)(const VubMeta&, std::span<const uint8>) = built.settings.receiveFn;
	static constexpr uint32 (*clockFn)() = built.settings.clockFn;


	explicit Vub(LinkType& linkObject) : link(&linkObject), blocks(Builder::MakeStorage(built.entries)) {
	}


	// Encodes one payload and queues it. Returns ok when it is queued, busy when the control queue is full (the payload is not queued and
	// nothing is counted: the caller sends it again later), messageTooLong when it is larger than MaxPayload, or the status of a block that
	// refused it.
	ResultStatus Send(std::span<const uint8> payload, const VubMeta& meta = VubMeta()) {
		if (payload.size() > built.settings.maxPayload) {
			return ResultStatus::messageTooLong;
		}

		if (VubQueueFull(control.indexes, sendSlots)) {
			return ResultStatus::busy;
		}

		return SendFrom<0>(payload, meta, VubLane::control, true);
	}


	// The main loop call. Reads the clock, takes the received bytes and packets up the chain, runs the layers' Process, and sends.
	void Process() {
		uint32 now = 0;
		if (clockFn != nullptr) {
			now = clockFn();
		}

		if constexpr (streamLink) {
			DrainRing();
		} else {
			DrainQueue();
		}

		ProcessLayers<0>(now);
		Egress();
	}


	// Takes one received byte. Called from the receive interrupt of a byte-stream link. A full ring drops the byte and counts it.
	void Feed(uint8 byte) requires (streamLink) {
		if (!VubByteRingPush(ring.indexes, ring.Span(), byte)) {
			VubCountIsr(ringFullIsr);
		}
	}


	// Takes one received packet. Called from the receive interrupt of a packet link. A packet longer than the receive buffer counts as too
	// long; a packet that finds the queue full counts as queue full.
	void FeedPacket(std::span<const uint8> packet) requires (!streamLink) {
		ResultStatus status = VubQueuePush(rxQueue.indexes, rxQueue.View(), packet, 0);
		if (status == ResultStatus::messageTooLong) {
			VubCountIsr(tooLongIsr);
		} else if (status == ResultStatus::busy) {
			VubCountIsr(queueFullIsr);
		}
	}


	// A copy of the counters.
	VubDrops Drops() const {
		VubDrops snapshot = drops;
		snapshot.ringFull = ringFullIsr;
		snapshot.queueFull = queueFullIsr;
		uint32 tooLongTotal = static_cast<uint32>(drops.tooLong) + tooLongIsr;
		snapshot.tooLong = tooLongTotal > 65535 ? 65535 : static_cast<uint16>(tooLongTotal);
		return snapshot;
	}


private:
	struct FramedState {
		uint16 length = 0;
		uint16 sent = 0;
		bool valid = false;
	};


	struct HeldState {
		uint16 length = 0;
		uint8 flags = 0;
		bool valid = false;
	};


	LinkType* link;
	[[no_unique_address]] Storage blocks;
	[[no_unique_address]] VubBuffer<hasSteps, packetBytes, 1> sendA;
	[[no_unique_address]] VubBuffer<hasSteps, packetBytes, 2> sendB;
	[[no_unique_address]] VubBuffer<hasBulk, packetBytes, 3> pulled;
	[[no_unique_address]] VubBuffer<hasBulk, packetBytes, 4> held;
	[[no_unique_address]] VubState<hasBulk, HeldState, 5> heldState;
	[[no_unique_address]] VubBuffer<streamLink && hasFraming, framedBytes, 6> framed;
	[[no_unique_address]] VubState<streamLink && hasFraming, FramedState, 7> framedState;
	[[no_unique_address]] VubQueueStorage<true, sendSlots, packetBytes, 8> control;
	[[no_unique_address]] VubBuffer<true, receiveBytes, 9> rx;
	[[no_unique_address]] VubRingStorage<streamLink, ringSlots, 10> ring;
	[[no_unique_address]] VubQueueStorage<!streamLink, receiveSlots, receiveBytes, 11> rxQueue;
	VubDrops drops;
	volatile uint16 ringFullIsr = 0;
	volatile uint16 queueFullIsr = 0;
	volatile uint16 tooLongIsr = 0;


	// The step with index From is applied to packet and its result goes into a step buffer, alternating between the two buffers. The
	// packet reaches the end of the chain (the framing or the link) and is staged on the lane. A packet with a caller (caller true)
	// returns its failure to that caller and is not counted. A packet with no caller (a layer's own packet, a bulk packet) is counted
	// where it is refused: a layer's Add under layerError, a transform's Encode under transformError.
	template<std::size_t From>
	ResultStatus SendFrom(std::span<const uint8> packet, const VubMeta& meta, VubLane lane, bool caller) {
		if constexpr (From >= count) {
			return Stage(packet, meta, lane);
		} else {
			using Entry = std::tuple_element_t<From, Tuple>;
			if constexpr (Entry::kind == VubKind::layer) {
				std::span<uint8> out = StepBuffer<From>();
				Result<std::size_t> added = std::get<From>(blocks).Add(meta, packet, out);
				if (added.IsErr()) {
					if (!caller) {
						VubCountDrop(drops.layerError);
					}

					return added.Error();
				}

				if (added.Value() > out.size()) {
					if (!caller) {
						VubCountDrop(drops.layerError);
					}

					return ResultStatus::messageTooLong;
				}

				return SendFrom<From + 1>(out.first(added.Value()), meta, lane, caller);
			} else if constexpr (Entry::kind == VubKind::transform) {
				std::span<uint8> out = StepBuffer<From>();
				Result<std::size_t> encoded = std::get<From>(blocks).Encode(meta, packet, out);
				if (encoded.IsErr()) {
					if (!caller) {
						VubCountDrop(drops.transformError);
					}

					return encoded.Error();
				}

				if (encoded.Value() > out.size()) {
					if (!caller) {
						VubCountDrop(drops.transformError);
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


	// Puts a finished packet on its lane: the control queue, or the one bulk slot.
	ResultStatus Stage(std::span<const uint8> packet, const VubMeta& meta, VubLane lane) {
		if (lane == VubLane::control) {
			return VubQueuePush(control.indexes, control.View(), packet, meta.flags);
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
			VubMeta meta{};
			receiveFn(meta, packet);
			return ResultStatus::ok;
		} else {
			using Entry = std::tuple_element_t<Below - 1, Tuple>;
			VubMeta meta{};
			if constexpr (Entry::kind == VubKind::transform) {
				Result<std::size_t> decoded = std::get<Below - 1>(blocks).Decode(meta, packet);
				if (decoded.IsErr()) {
					VubCountDrop(drops.transformError);
					return decoded.Error();
				}

				if (decoded.Value() > packet.size()) {
					VubCountDrop(drops.transformError);
					return ResultStatus::invalidData;
				}

				return ReceiveBelow<Below - 1>(packet.first(decoded.Value()));
			} else if constexpr (Entry::kind == VubKind::layer) {
				Result<VubTake> taken = std::get<Below - 1>(blocks).Take(meta, packet);
				if (taken.IsErr()) {
					VubCountDrop(drops.layerError);
					return taken.Error();
				}

				if (!taken.Value().deliver) {
					return ResultStatus::ok;
				}

				if (taken.Value().consumed > packet.size()) {
					VubCountDrop(drops.layerError);
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


	// Takes the received bytes out of the ring, through the framing. A byte-stream link only.
	void DrainRing() {
		if constexpr (streamLink) {
			uint8 value = 0;
			while (VubByteRingPop(ring.indexes, ring.Span(), value)) {
				std::size_t length = 0;
				VubFrame state = std::get<framingIndex>(blocks).Push(value, rx.Span(), length);
				switch (state) {
					case VubFrame::packet:
						ReceivePacket(rx.Span().first(length));
						break;
					case VubFrame::broken:
						VubCountDrop(drops.framingError);
						break;
					case VubFrame::tooLong:
						VubCountDrop(drops.tooLong);
						break;
					case VubFrame::pending:
						break;
				}
			}
		}
	}


	// Takes the received packets out of the queue. A packet link only.
	void DrainQueue() {
		if constexpr (!streamLink) {
			std::span<const uint8> packet;
			uint8 flags = 0;
			while (VubQueueFront(rxQueue.indexes, rxQueue.View(), packet, flags)) {
				std::size_t length = packet.size();
				std::span<uint8> slot = rx.Span();
				for (std::size_t index = 0; index < length; index++) {
					slot[index] = packet[index];
				}

				VubQueuePop(rxQueue.indexes, receiveSlots);
				ReceivePacket(slot.first(length));
			}
		}
	}


	template<std::size_t Index>
	void ProcessLayers(uint32 now) {
		if constexpr (Index < count) {
			using Entry = std::tuple_element_t<Index, Tuple>;
			if constexpr (Entry::kind == VubKind::layer) {
				VubOutbox outbox(this, &Vub::template SendThunk<Index + 1>);
				std::get<Index>(blocks).Process(now, outbox);
			}

			ProcessLayers<Index + 1>(now);
		}
	}


	// The outbox of the layer at Index - 1 sends into the steps below it. The packet is at the layer's level, so it starts at the step
	// after the layer.
	// The outbox of a layer. A packet with no caller: one the layer could not send is counted, and busy is not, since the layer keeps
	// the packet and sends it again.
	template<std::size_t From>
	static ResultStatus SendThunk(void* owner, std::span<const uint8> packet, const VubMeta& meta) {
		Vub* self = static_cast<Vub*>(owner);
		if (packet.size() > packetBytes) {
			VubCountDrop(self->drops.layerError);
			return ResultStatus::messageTooLong;
		}

		if (VubQueueFull(self->control.indexes, sendSlots)) {
			return ResultStatus::busy;
		}

		return self->template SendFrom<From>(packet, meta, VubLane::control, false);
	}


	// Sends from the queues while the link accepts: control first, then the held bulk packet, then a new bulk packet from a layer. Every
	// packet taken from a queue or a layer counts toward PerProcess, whether the link took it or refused it, so one call stops within
	// PerProcess packets.
	void Egress() {
		std::size_t sentPackets = 0;
		while (sentPackets < perProcess) {
			if constexpr (streamLink && hasFraming) {
				if (framedState.Get().valid) {
					if (!WriteFramed()) {
						return;
					}

					continue;
				}
			}

			std::span<const uint8> packet;
			uint8 flags = 0;
			if (VubQueueFront(control.indexes, control.View(), packet, flags)) {
				if (!Offer(packet, flags)) {
					return;
				}

				VubQueuePop(control.indexes, sendSlots);
				sentPackets++;
				continue;
			}

			if constexpr (hasBulk) {
				if (heldState.Get().valid) {
					std::span<const uint8> bulk(held.Span().data(), heldState.Get().length);
					if (!Offer(bulk, heldState.Get().flags)) {
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


	// Gives one packet to the link. On a byte-stream link the packet is framed into the framed buffer, and WriteFramed sends it. Returns
	// false when a packet link is busy: the packet stays where it is.
	bool Offer(std::span<const uint8> packet, uint8 flags) {
		if constexpr (streamLink) {
			if constexpr (hasFraming) {
				VubMeta meta{};
				meta.flags = flags;
				Result<std::size_t> length = std::get<framingIndex>(blocks).Frame(meta, packet, framed.Span());
				if (length.IsErr()) {
					VubCountDrop(drops.sendError);
					return true;
				}

				framedState.Get().length = static_cast<uint16>(length.Value());
				framedState.Get().sent = 0;
				framedState.Get().valid = true;
			}

			return true;
		} else {
			ResultStatus status = link->Send(packet);
			if (status == ResultStatus::busy) {
				return false;
			}

			if (status != ResultStatus::ok) {
				VubCountDrop(drops.sendError);
			}

			return true;
		}
	}


	// Writes the rest of the framed packet. Returns true when the packet is complete, false when the link took less than the rest.
	bool WriteFramed() {
		if constexpr (streamLink && hasFraming) {
			FramedState& state = framedState.Get();
			std::span<const uint8> rest = std::span<const uint8>(framed.Span()).subspan(state.sent, state.length - state.sent);
			Result<std::size_t> written = link->Write(rest);
			if (written.IsErr()) {
				VubCountDrop(drops.sendError);
				state.valid = false;
				return true;
			}

			std::size_t taken = written.Value() < rest.size() ? written.Value() : rest.size();
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
			if constexpr (Entry::kind == VubKind::layer && Entry::MakesBulk()) {
				VubMeta meta{};
				std::span<uint8> out = pulled.Span();
				Result<std::size_t> produced = std::get<Index>(blocks).Next(meta, out);
				if (produced.IsOk()) {
					if (produced.Value() > out.size()) {
						VubCountDrop(drops.layerError);
					} else if (SendFrom<Index + 1>(out.first(produced.Value()), meta, VubLane::bulk, false) == ResultStatus::ok) {
						return 0;
					}

					// The packet was dropped and counted. The layers below may still have data in this call, within the room left.
					return room > 1 ? 1 + PullFrom<Index + 1>(room - 1) : 1;
				}

				if (produced.Error() != ResultStatus::noDataAvailable) {
					VubCountDrop(drops.layerError);
				}
			}

			return PullFrom<Index + 1>(room);
		}
	}
};
