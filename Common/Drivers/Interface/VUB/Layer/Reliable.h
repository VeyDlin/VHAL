#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>

namespace VUB {


// What the application learns about a packet it sent with delivery, once. status ok: the peer acknowledged it, and it was passed up.
// retryExhausted: no acknowledgement came after the last resend, so the packet may or may not have been passed up; it is not sent again.
// reset: the peer lost the session while the packet was not acknowledged, so it may or may not have arrived; it is not sent again. payload
// is the packet's payload, valid only during the call. attempts is how many times it was sent. owner is the owner pointer of the layer
// above, set when that layer declares wantsOwner (null otherwise).
struct Outcome {
	ResultStatus status;
	uint16 sequence;
	uint16 attempts;
	std::span<const uint8> payload;
	void* owner = nullptr;
};


// Plain helpers of the delivery layer. They do not depend on the layer's template arguments, so each is one function in the program,
// whatever the number of layers.
namespace ReliableWire {
	// The session tag: the counter plus the low byte of the boot value. Sessions of one boot have different tags for 256 sessions.
	inline uint8 TagOf(uint16 boot, uint8 counter) {
		return static_cast<uint8>(counter + static_cast<uint8>(boot & 0xFF));
	}


	// The first number of the sessions of a boot value: the boot value times an odd factor, so different boot values have different first
	// numbers in 16 bits. The layer masks it to its sequence width.
	inline uint16 FirstOf(uint16 boot) {
		return static_cast<uint16>(static_cast<uint32>(boot) * 0x9E37u);
	}


	// True when the clock has reached due. The difference is taken with wrap, so a clock that wraps still compares right.
	inline bool Due(uint32 nowUs, uint32 dueUs) {
		return static_cast<int32>(nowUs - dueUs) >= 0;
	}


	inline void CopyBytes(const uint8* from, std::size_t count, uint8* to) {
		for (std::size_t index = 0; index < count; index++) {
			to[index] = from[index];
		}
	}
}


// The handlers of the options are held as the address of a variable that holds the function. Some compilers (g++ 11) take the address of a
// variable as a template argument, but not the address of a function, and an options value is a template argument. The variable is
// constexpr, so a call through it is a call of the function.
template<void (*Fn)(const Outcome&)>
inline constexpr void (*OutcomeHandler)(const Outcome&) = Fn;


template<void (*Fn)()>
inline constexpr void (*RestartHandler)() = Fn;


// The same for the restart handler that takes the owner pointer of the layer above.
template<void (*Fn)(void*)>
inline constexpr void (*RestartOwnerHandler)(void*) = Fn;


// The same for the question whether a packet is still wanted, which the layer above answers with DeliveryWanted.
template<bool (*Fn)(void*, std::span<const uint8>)>
inline constexpr bool (*WantedHandler)(void*, std::span<const uint8>) = Fn;


// The options of the delivery layer. The layer takes them as its last template argument, so each option is a constant of the layer: an
// option that is off costs no code and no RAM. The fields are structural (integers and addresses of variables), so a value of this type can
// be a template argument. The defaults are the layer's defaults for the two capacities. wantsOwner and the two handlers of the owner are set
// by the stack, for a layer above that declares wantsOwner; a user handler on such a layer is refused.
template<std::size_t PayloadCapacity, std::size_t WindowCapacity>
struct ReliableOptions {
	uint8 retries = 3;
	uint16 timeoutMs = 200;
	uint8 window = WindowCapacity < 4 ? static_cast<uint8>(WindowCapacity) : static_cast<uint8>(4);
	uint8 sequenceBits = 16;
	uint16 backoffMaxMs = 0;
	uint16 payloadBytes = PayloadCapacity;
	bool counted = false;
	bool wantsOwner = false;
	void (* const* outcomeFn)(const Outcome&) = nullptr;
	void (* const* restartFn)() = nullptr;
	void (* const* restartOwnerFn)(void*) = nullptr;
	bool (* const* wantedFn)(void*, std::span<const uint8>) = nullptr;
};


// True when a handler option holds a handler. The option is passed to a function and tested there, so the test is not written against the
// address of the holder variable at the call site: g++ 11 reports that address as never null under -Waddress.
template<typename Pointer>
constexpr bool HoldsHandler(Pointer pointer) {
	return pointer != nullptr;
}


// The delivery layer. Each packet carries a sequence number, the session tag of its sender and the sender's window base. The receiver
// acknowledges each packet (with InOrder, a held packet when it goes up), and the sender resends it after a timeout, up to a number of
// times, then reports retryExhausted. A session starts with a sync packet, which carries the sender's boot value and the session
// counter, and Send returns noInit until SetSession has given the boot value. The window of unacknowledged packets, the
// sizes and the options are described on the wiki page. PayloadCapacity and WindowCapacity are the sizes of the layer's storage: the
// options choose within them, and an option that is off costs no more RAM than the storage the type already has. InOrder = true passes
// the packets up in sequence order: it holds the packets that arrive ahead of a gap, and it costs WindowCapacity payloads of RAM.
// Value is the layer's options. The stack stores a layer of a chain as Configured, with the options of its chain entry, so the
// options are constants of the stored layer.
template<
	std::size_t PayloadCapacity = 32,
	std::size_t WindowCapacity = 4,
	bool InOrder = false,
	ReliableOptions<PayloadCapacity, WindowCapacity> Value = ReliableOptions<PayloadCapacity, WindowCapacity>{}>
class ReliableOf {
public:
	using Options = ReliableOptions<PayloadCapacity, WindowCapacity>;

	// The layer with other options, as a template argument. The stack uses it for a layer of a chain given options.
	template<Options value>
	using Configured = ReliableOf<PayloadCapacity, WindowCapacity, InOrder, value>;

	static constexpr Options options = Value;
	static constexpr std::size_t header = 11;
	static constexpr uint32 fingerprint = 0x52454C33;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = false;
	static constexpr bool passesUp = InOrder;

	// The layer above asks whether a packet is still wanted (DeliveryWanted). Then the layer's resends go out in the order of their numbers,
	// and its copies in a queue are marked, so the stack drops them when the connection returns and the layer sends the wanted ones again.
	static constexpr bool resendsWanted = HoldsHandler(options.wantedFn);

	// The flags in the first byte of every packet of the layer.
	static constexpr uint8 SequencedFlag = 0x01;
	static constexpr uint8 AcknowledgesFlag = 0x02;
	static constexpr uint8 SyncFlag = 0x04;
	static constexpr uint8 ResyncFlag = 0x08;
	static constexpr uint8 ControlFlag = 0x10;


	// The option methods of the block, right after it in the chain. Each one changes one field of the options.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		// Resends after the first timeout, up to this many times. 0 gives up at the first timeout. With the in-order option 0 is refused:
		// a held packet is acknowledged only when it goes up, so it would be given up ahead of its gap filling.
		constexpr Step Retries(uint8 count) const {
			Require<"Retries(0) with the in-order option: a held packet is given up before its gap fills">(count >= 1 || !InOrder);
			Options next = this->LastOptions();
			next.retries = count;
			return Step(this->WithLastOptions(next));
		}


		// The timeout of a packet until its first resend, in milliseconds.
		constexpr Step TimeoutMs(uint16 milliseconds) const {
			Require<"TimeoutMs() is zero">(milliseconds > 0);
			Options next = this->LastOptions();
			next.timeoutMs = milliseconds;
			return Step(this->WithLastOptions(next));
		}


		// The packets that may be unacknowledged at once. The receiver accepts packets this far ahead of its oldest gap. The default is 4,
		// or WindowCapacity when that is smaller: with WindowCapacity 1 the window is 1.
		constexpr Step Window(uint8 count) const {
			Require<"Window() is zero, or larger than the layer's WindowCapacity or 32">(
				count >= 1 && count <= WindowCapacity && count <= 32);
			Options next = this->LastOptions();
			next.window = count;
			return Step(this->WithLastOptions(next));
		}


		// The width of the sequence and acknowledgement numbers: 8 or 16 bits. Use 8 only on a path that keeps the send order.
		constexpr Step SequenceBits(uint8 bits) const {
			Require<"SequenceBits() is not 8 or 16">(bits == 8 || bits == 16);
			Options next = this->LastOptions();
			next.sequenceBits = bits;
			return Step(this->WithLastOptions(next));
		}


		// Backoff: the timeout doubles after each try, up to this many milliseconds. 0 keeps the timeout fixed.
		constexpr Step BackoffMaxMs(uint16 milliseconds) const {
			Options next = this->LastOptions();
			next.backoffMaxMs = milliseconds;
			return Step(this->WithLastOptions(next));
		}


		// The largest payload the layer sends. A larger payload is refused with messageTooLong.
		constexpr Step PayloadBytes(uint16 bytes) const {
			Require<"PayloadBytes() is larger than the layer's PayloadCapacity">(bytes <= PayloadCapacity);
			Options next = this->LastOptions();
			next.payloadBytes = bytes;
			return Step(this->WithLastOptions(next));
		}


		// The handler of the outcome of each packet sent with delivery: a function, given as a template argument.
		template<void (*Fn)(const Outcome&)>
		constexpr Step OnOutcome() const {
			Options next = this->LastOptions();
			next.outcomeFn = &OutcomeHandler<Fn>;
			return Step(this->WithLastOptions(next));
		}


		// The handler called when the peer lost the session: its state was lost, and the layer starts a session again.
		template<void (*Fn)()>
		constexpr Step OnRestart() const {
			Options next = this->LastOptions();
			next.restartFn = &RestartHandler<Fn>;
			return Step(this->WithLastOptions(next));
		}


		// The same restart, with the owner pointer of the layer above (the owner is null unless that layer declares wantsOwner).
		template<void (*Fn)(void*)>
		constexpr Step OnRestartOwner() const {
			Options next = this->LastOptions();
			next.restartOwnerFn = &RestartOwnerHandler<Fn>;
			return Step(this->WithLastOptions(next));
		}


		// Counts the resends and the peer restarts of the layer: Resends() and PeerRestarts() read them, and a layer above that declares
		// usesCounters gets them through CountingOutbox::Counters(). Without it no counter is kept.
		constexpr Step Counted() const {
			Options next = this->LastOptions();
			next.counted = true;
			return Step(this->WithLastOptions(next));
		}
	};


	// The options of the layer with the owner of the layer above (wantsOwner): its outcomes go to Above::DeliveryOutcome and its restarts to
	// Above::DeliveryRestart, which get the owner pointer. A layer above that declares DeliveryWanted is asked whether a packet is still
	// wanted when the packet is due to be sent again. A handler the application gave this layer is refused, since the layer above takes them.
	template<typename Above>
	static constexpr Options OwnedBy(Options value) {
		Require<"the layer below a layer with wantsOwner has an OnOutcome or OnRestart: the layer with wantsOwner takes them, remove the handler">(
			!HoldsHandler(value.outcomeFn) && !HoldsHandler(value.restartFn) && !HoldsHandler(value.restartOwnerFn));
		value.wantsOwner = true;
		value.outcomeFn = &OutcomeHandler<&Above::DeliveryOutcome>;
		value.restartOwnerFn = &RestartOwnerHandler<&Above::DeliveryRestart>;
		if constexpr (requires { &Above::DeliveryWanted; }) {
			value.wantedFn = &WantedHandler<&Above::DeliveryWanted>;
		}

		return value;
	}


	// The payload capacity of the layer's storage: the largest payload its storage holds. A layer above that declares largestPacket is
	// checked against it by the builder.
	static constexpr std::size_t payloadCapacity = PayloadCapacity;


private:
	enum class SlotState : uint8 {
		free,
		pending,
		acked,
		reset
	};


	// One packet waiting for its acknowledgement. bytes holds the header area first (header bytes), then the payload. A sync has no payload.
	struct Slot {
		std::array<uint8, header + PayloadCapacity> bytes{};
		uint16 sequence = 0;
		uint16 length = 0;
		uint16 attempts = 0;
		uint32 dueUs = 0;
		SlotState state = SlotState::free;
		bool sync = false;
	};


	// The packets held for in-order delivery: one payload for each window slot. The slot of the packet at the base is head; the packet
	// after it is in the next slot, and so on. syncAcked is set when the acknowledgement of a sync arrives: the session is established in
	// Process, once the held packets ahead of it have gone up, so no packet of the session goes out before them.
	struct HeldStore {
		std::array<std::array<uint8, PayloadCapacity>, WindowCapacity> payloads{};
		std::array<uint16, WindowCapacity> lengths{};
		uint8 head = 0;
		bool syncAcked = false;
	};


	// The store of a layer that does not hold packets: it has no size.
	struct NoHeldStore {
	};


	// The owner pointer of the layer above, given to the outcomes and restarts. A layer without wantsOwner has none.
	struct OwnerPointer {
		void* owner = nullptr;
	};


	struct NoOwner {
	};


	// The resends and peer restarts counted by Counted(). A layer that is not counted has none.
	struct CountStore {
		uint16 resends = 0;
		uint16 peerRestarts = 0;
	};


	struct NoCountStore {
	};


	// The fields of a header. The fields that the flags name are written; the others are ignored.
	struct Fields {
		uint8 flags = 0;
		uint8 tag = 0;
		uint16 sequence = 0;
		uint8 delta = 0;
		uint16 boot = 0;
		uint8 counter = 0;
		uint8 ackTag = 0;
		uint16 ack = 0;
	};


	// The state kept for one peer: the sending side (its session and window) and the receiving side (the session it holds and the
	// acknowledgements owed). The layer has no table of peers.
	struct Peer {
		// Sending side. A session starts with a sync: established is set when the sync is acknowledged (with InOrder, in Process, once the
		// held packets have gone up: see HeldStore), and until then Send is busy.
		// newSession is set when the next sync starts a session with a counter of its own. The boot value is given with SetSession, and
		// bootGiven says it was; until then Send returns noInit. The tag is derived from the boot value and the counter.
		bool needSync = false;
		bool newSession = true;
		bool established = false;
		bool bootGiven = false;
		uint16 bootValue = 0;
		uint8 nextCounter = 0;
		uint8 counter = 0;
		uint8 tag = 0;
		uint16 nextSequence = 0;
		uint8 inFlight = 0;
		std::array<Slot, WindowCapacity> slots{};

		// Receiving side: the session it holds (its boot value and counter, and the tag derived from them). base is the lowest number not
		// received, seen has bit i for base + i (i below the window).
		bool open = false;
		uint16 heldBoot = 0;
		uint8 heldCounter = 0;
		uint8 receiveTag = 0;
		uint16 base = 0;
		uint32 seen = 0;
		bool owesResync = false;
		uint8 resyncTag = 0;
		bool owesCounter = false;
		std::array<uint16, 2 * WindowCapacity> owed{};
		std::array<uint8, 2 * WindowCapacity> owedTags{};
		std::size_t owedHead = 0;
		std::size_t owedCount = 0;

		bool restartReport = false;
	};


	// The receiving side keeps bits for the packets ahead of the base. A window of 1 has none, unless the in-order option holds a packet at
	// the base. Without the bits the code for them is not in the layer.
	static constexpr bool TracksAhead = WindowCapacity > 1 || InOrder;


	Peer peer;
	[[no_unique_address]] std::conditional_t<InOrder, HeldStore, NoHeldStore> held{};
	[[no_unique_address]] std::conditional_t<Value.wantsOwner, OwnerPointer, NoOwner> ownerSlot{};
	[[no_unique_address]] std::conditional_t<Value.counted, CountStore, NoCountStore> counts{};
	bool linkUp = true;
	uint32 lastNowUs = 0;


public:
	// Puts the payload into a packet with a sequence number, and keeps it until it is acknowledged. The packet is returned for the blocks
	// below, which send it first. Returns noInit until SetSession has been called, and nothing is sent. Returns busy while the session is
	// starting or the window is full, and messageTooLong for a payload over PayloadBytes. The first Send that returns busy starts the
	// session: its sync goes out at the next Process.
	Result<std::size_t> Add(const Meta& meta, std::span<const uint8> in, std::span<uint8> out) {
		if (!peer.bootGiven) {
			return ResultStatus::noInit;
		}

		if (in.size() > options.payloadBytes) {
			return ResultStatus::messageTooLong;
		}

		uint16 ack = 0;
		uint8 ackTag = 0;
		bool piggyback = PeekOwed(ack, ackTag);
		if ((meta.flags & Meta::NoAcknowledgeFlag) != 0) {
			Fields fields{};
			fields.flags = piggyback ? AcknowledgesFlag : 0;
			fields.ackTag = ackTag;
			fields.ack = ack;
			std::array<uint8, header> head{};
			std::size_t headLength = PutHeader(head, fields);
			if (out.size() < headLength + in.size()) {
				return ResultStatus::invalidParameter;
			}

			ReliableWire::CopyBytes(head.data(), headLength, out.data());
			ReliableWire::CopyBytes(in.data(), in.size(), out.data() + headLength);
			if (piggyback) {
				PopOwed();
			}

			return headLength + in.size();
		}

		if (!peer.established) {
			if (!SyncInFlight() && !SyncAwaitingRelease()) {
				peer.needSync = true;
			}

			return ResultStatus::busy;
		}

		// With one slot, a pending packet already takes the only free slot, so the span check adds nothing.
		if (peer.inFlight >= options.window || (WindowCapacity > 1 && SpanExceeded(peer.nextSequence))) {
			return ResultStatus::busy;
		}

		uint16 sequence = peer.nextSequence;
		Fields fields{};
		fields.flags = static_cast<uint8>(SequencedFlag | (piggyback ? AcknowledgesFlag : 0));
		fields.tag = peer.tag;
		fields.sequence = sequence;
		fields.delta = BaseDelta(sequence);
		fields.ackTag = ackTag;
		fields.ack = ack;
		std::array<uint8, header> head{};
		std::size_t headLength = PutHeader(head, fields);
		if (out.size() < headLength + in.size()) {
			return ResultStatus::invalidParameter;
		}

		Slot* slot = FindFree();
		if (slot == nullptr) {
			return ResultStatus::busy;
		}

		peer.nextSequence = static_cast<uint16>((sequence + 1) & Mask());
		slot->sequence = sequence;
		slot->length = static_cast<uint16>(in.size());
		slot->sync = false;
		slot->attempts = 1;
		slot->dueUs = lastNowUs + TimeoutUs(1);
		slot->state = SlotState::pending;
		peer.inFlight++;
		ReliableWire::CopyBytes(in.data(), in.size(), slot->bytes.data() + header);
		ReliableWire::CopyBytes(head.data(), headLength, out.data());
		ReliableWire::CopyBytes(in.data(), in.size(), out.data() + headLength);
		if (piggyback) {
			PopOwed();
		}

		return headLength + in.size();
	}


	// Takes the header off a received packet. A sequenced packet of the session held for its sender is passed up when it is inside the
	// window and not a duplicate; a duplicate is acknowledged again and not passed up; a sync is acknowledged and not passed up; an
	// acknowledgement or a resync request is taken and not passed up. A packet of another session is not passed up and not acknowledged.
	Result<Taken> Take(const Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		uint8 flags = packet[0];
		std::size_t seqBytes = SeqBytes();
		if (flags == ResyncFlag) {
			if (packet.size() != 2) {
				return ResultStatus::invalidData;
			}

			ReceiveResync(packet[1]);
			return Taken{2, false};
		}

		if (flags == (ResyncFlag | ControlFlag)) {
			if (packet.size() != 4) {
				return ResultStatus::invalidData;
			}

			ReceiveCounterReply(GetLe16(packet.data() + 1), packet[3]);
			return Taken{4, false};
		}

		if (flags == (ControlFlag | AcknowledgesFlag)) {
			if (packet.size() != 2 + seqBytes) {
				return ResultStatus::invalidData;
			}

			ReceiveAcknowledgement(packet[1], GetSequence(packet.data() + 2));
			return Taken{2 + seqBytes, false};
		}

		if ((flags & 0xE0) != 0 || (flags & (ResyncFlag | ControlFlag)) != 0) {
			return ResultStatus::invalidData;
		}

		bool sequenced = (flags & SequencedFlag) != 0;
		bool acknowledges = (flags & AcknowledgesFlag) != 0;
		bool sync = (flags & SyncFlag) != 0;
		if (!sequenced && sync) {
			return ResultStatus::invalidData;
		}

		std::size_t at = 1;
		uint8 tag = 0;
		uint16 sequence = 0;
		uint8 delta = 0;
		uint16 boot = 0;
		uint8 counter = 0;
		if (sequenced) {
			if (packet.size() < at + 2 + seqBytes) {
				return ResultStatus::invalidData;
			}

			tag = packet[at];
			sequence = GetSequence(packet.data() + at + 1);
			delta = packet[at + 1 + seqBytes];
			at += 2 + seqBytes;
			if (sync) {
				if (packet.size() < at + 3) {
					return ResultStatus::invalidData;
				}

				boot = GetLe16(packet.data() + at);
				counter = packet[at + 2];
				at += 3;
			}
		}

		uint8 ackTag = 0;
		uint16 ack = 0;
		if (acknowledges) {
			if (packet.size() < at + 1 + seqBytes) {
				return ResultStatus::invalidData;
			}

			ackTag = packet[at];
			ack = GetSequence(packet.data() + at + 1);
			at += 1 + seqBytes;
		}

		if (sync && (packet.size() != at || delta != 0 || tag != ReliableWire::TagOf(boot, counter))) {
			return ResultStatus::invalidData;
		}

		if (acknowledges) {
			ReceiveAcknowledgement(ackTag, ack);
		}

		if (!sequenced) {
			return Taken{at, true};
		}

		bool deliver = ReceiveSequenced(tag, sequence, delta, sync, boot, counter, packet.subspan(at));
		return Taken{at, deliver};
	}


	// Sends what is owed: the resync request, the sync of a session that is starting, the acknowledgements that did not ride on a packet,
	// and the resends that are due. Settles the outcomes of the packets acknowledged or reset since the last call. While the connection is
	// down nothing is sent and no timeout runs.
	void Process(uint32 nowUs, const Outbox& outbox) {
		lastNowUs = nowUs;
		SettleSlots();
		if (peer.restartReport) {
			peer.restartReport = false;
			if constexpr (HoldsHandler(options.restartFn)) {
				(*options.restartFn)();
			}

			if constexpr (HoldsHandler(options.restartOwnerFn)) {
				(*options.restartOwnerFn)(OwnerValue());
			}
		}

		if constexpr (InOrder) {
			if (ReleaseInOrder(outbox) && held.syncAcked) {
				held.syncAcked = false;
				peer.established = true;
			}
		}

		if (!linkUp) {
			return;
		}

		if (peer.owesResync) {
			std::array<uint8, 2> request = {ResyncFlag, peer.resyncTag};
			if (outbox.Send(std::span<const uint8>(request.data(), request.size())) != ResultStatus::busy) {
				peer.owesResync = false;
			}
		}

		if (peer.owesCounter) {
			std::array<uint8, 4> reply = {static_cast<uint8>(ResyncFlag | ControlFlag), 0, 0, peer.heldCounter};
			PutLe16(reply.data() + 1, peer.heldBoot);
			if (outbox.Send(std::span<const uint8>(reply.data(), reply.size())) != ResultStatus::busy) {
				peer.owesCounter = false;
			}
		}

		StartSync();
		FlushAcknowledgements(outbox);
		RunTimers(nowUs, outbox);
	}


	// The connection changed. Down: nothing is sent until up. Up: every packet still waiting is due at once, so it is resent in this call.
	void OnLink(bool up, uint32 nowUs) {
		lastNowUs = nowUs;
		linkUp = up;
		if (up) {
			for (Slot& slot : peer.slots) {
				if (slot.state == SlotState::pending) {
					// With a layer above that asks DeliveryWanted, the try that was under way when the connection was lost does not count: its copy
					// waited in the stack's queue or was lost with the link. The send at the return takes the try's place.
					if constexpr (resendsWanted) {
						if (slot.attempts > 0) {
							slot.attempts--;
						}
					}

					slot.dueUs = nowUs;
				}
			}
		}
	}


	// The boot value of the sessions this layer starts. It must differ for each boot: a random number, or the chip's id mixed with a boot
	// counter kept in flash. Call it ahead of the first send; until then Send returns noInit and no session starts. The value applies to the
	// sessions that start after the call.
	void SetSession(uint16 value) {
		peer.bootValue = value;
		peer.bootGiven = true;
	}


	// The owner pointer that the outcomes and the restart handler get: the layer above, given by the stack when it is built. Present only
	// when the layer above declares wantsOwner.
	void SetOwner(void* owner) requires (options.wantsOwner) {
		ownerSlot.owner = owner;
	}


	// The packets this layer sent again, counted from the first resend. Present only when the layer is Counted().
	uint16 Resends() const requires (options.counted) {
		return counts.resends;
	}


	// The peer restarts this layer reported. Present only when the layer is Counted().
	uint16 PeerRestarts() const requires (options.counted) {
		return counts.peerRestarts;
	}


private:
	// The owner pointer for the outcomes and restarts: the stored one when the layer above wants it, null otherwise.
	void* OwnerValue() const {
		if constexpr (options.wantsOwner) {
			return ownerSlot.owner;
		} else {
			return nullptr;
		}
	}


	// A peer restart is reported: it is counted when the layer is counted.
	void ReportRestart() {
		peer.restartReport = true;
		if constexpr (options.counted) {
			CountDrop(counts.peerRestarts);
		}
	}


	// The sequence width in bytes.
	std::size_t SeqBytes() const {
		if constexpr (options.sequenceBits == 8) {
			return 1;
		} else {
			return 2;
		}
	}


	uint16 Mask() const {
		if constexpr (options.sequenceBits == 8) {
			return static_cast<uint16>(0xFF);
		} else {
			return static_cast<uint16>(0xFFFF);
		}
	}


	// The timeout ahead of try number attempts. It doubles after each try up to the backoff cap, when there is one. Without a cap the
	// timeout is fixed, and the code of the backoff is not in the layer.
	uint32 TimeoutUs(uint16 attempts) const {
		uint32 timeout = static_cast<uint32>(options.timeoutMs) * 1000u;
		if constexpr (options.backoffMaxMs == 0) {
			return timeout;
		} else {
			uint32 cap = static_cast<uint32>(options.backoffMaxMs) * 1000u;
			for (uint16 index = 1; index < attempts && timeout < cap; index++) {
				uint32 doubled = timeout * 2u;
				timeout = doubled > cap ? cap : doubled;
			}

			return timeout;
		}
	}


	// The oldest packet the sender has not settled: the oldest pending packet, or the next number when none is pending.
	uint16 SenderBase() const {
		uint16 mask = Mask();
		uint16 base = peer.nextSequence;
		uint16 oldest = 0;
		for (const Slot& slot : peer.slots) {
			if (slot.state != SlotState::pending) {
				continue;
			}

			uint16 distance = static_cast<uint16>((peer.nextSequence - slot.sequence) & mask);
			if (distance > oldest) {
				oldest = distance;
				base = slot.sequence;
			}
		}

		return base;
	}


	// The window base of a packet with the number sequence, as the delta from it. The sender keeps every pending packet within one window,
	// so the delta is smaller than the window.
	uint8 BaseDelta(uint16 sequence) const {
		return static_cast<uint8>((sequence - SenderBase()) & Mask());
	}


	// Writes the header of a packet: the flags, then the fields the flags name, in this order: for a sequenced packet the tag, the number
	// and the window base delta, for a sync the boot value and the counter, for an acknowledgement its tag and number. Returns its length.
	std::size_t PutHeader(std::array<uint8, header>& head, const Fields& fields) const {
		std::size_t at = 0;
		head[at] = fields.flags;
		at++;
		if ((fields.flags & SequencedFlag) != 0) {
			head[at] = fields.tag;
			at++;
			PutSequence(head.data() + at, fields.sequence);
			at += SeqBytes();
			head[at] = fields.delta;
			at++;
			if ((fields.flags & SyncFlag) != 0) {
				PutLe16(head.data() + at, fields.boot);
				at += 2;
				head[at] = fields.counter;
				at++;
			}
		}

		if ((fields.flags & AcknowledgesFlag) != 0) {
			head[at] = fields.ackTag;
			at++;
			PutSequence(head.data() + at, fields.ack);
			at += SeqBytes();
		}

		return at;
	}


	void PutSequence(uint8* at, uint16 value) const {
		at[0] = static_cast<uint8>(value & 0xFF);
		if (SeqBytes() == 2) {
			at[1] = static_cast<uint8>(value >> 8);
		}
	}


	uint16 GetSequence(const uint8* at) const {
		if (SeqBytes() == 2) {
			return GetLe16(at);
		}

		return static_cast<uint16>(at[0]);
	}


	Slot* FindFree() {
		for (Slot& slot : peer.slots) {
			if (slot.state == SlotState::free) {
				return &slot;
			}
		}

		return nullptr;
	}


	Slot* FindPending(uint16 sequence) {
		for (Slot& slot : peer.slots) {
			if (slot.state == SlotState::pending && slot.sequence == sequence) {
				return &slot;
			}
		}

		return nullptr;
	}


	bool SyncInFlight() const {
		for (const Slot& slot : peer.slots) {
			if (slot.state == SlotState::pending && slot.sync) {
				return true;
			}
		}

		return false;
	}


	// True when the sync is acknowledged and the session waits for the held packets ahead of it to go up (the in-order option only).
	bool SyncAwaitingRelease() const {
		if constexpr (InOrder) {
			return held.syncAcked;
		} else {
			return false;
		}
	}


	// The acknowledgements owed to the peer, a ring of 2 * WindowCapacity numbers, each with the tag of its session. PushOwed drops the
	// number when the ring is full: the peer resends the packet, and it is acknowledged then.
	bool PeekOwed(uint16& value, uint8& tag) const {
		if (peer.owedCount == 0) {
			return false;
		}

		value = peer.owed[peer.owedHead];
		tag = peer.owedTags[peer.owedHead];
		return true;
	}


	void PopOwed() {
		peer.owedHead = (peer.owedHead + 1) % peer.owed.size();
		peer.owedCount--;
	}


	void PushOwed(uint16 value, uint8 tag) {
		if (peer.owedCount == peer.owed.size()) {
			return;
		}

		std::size_t index = (peer.owedHead + peer.owedCount) % peer.owed.size();
		peer.owed[index] = value;
		peer.owedTags[index] = tag;
		peer.owedCount++;
	}


	// An acknowledgement is taken when its tag is the tag of the sender's current session. The acknowledgement of the sync establishes
	// the session. With InOrder the session is established by Process, after the held packets have gone up (see HeldStore).
	void ReceiveAcknowledgement(uint8 tag, uint16 sequence) {
		if (tag != peer.tag) {
			return;
		}

		Slot* slot = FindPending(sequence);
		if (slot != nullptr) {
			slot->state = SlotState::acked;
			if (slot->sync) {
				if constexpr (InOrder) {
					held.syncAcked = true;
				} else {
					peer.established = true;
				}
			}
		}
	}


	// A resync request names the session it refers to. One for another tag is not for this session, and is ignored. One for the current
	// tag, while the session is established, means the peer lost the session: the packets waiting for acknowledgement are reset, and the next
	// Send starts a session with a counter of its own.
	void ReceiveResync(uint8 tag) {
		if (!peer.established || tag != peer.tag) {
			return;
		}

		ResetSending();
	}


	// The peer lost the session of the sending side. The session is not established, the packets waiting for acknowledgement are
	// reset, and the next Send starts a session with a counter of its own. A sync still waiting is dropped with no outcome: the session it
	// would start is one the peer does not hold. The restart handler runs at the next Process.
	void ResetSending() {
		peer.established = false;
		peer.newSession = true;
		if constexpr (InOrder) {
			held.syncAcked = false;
		}

		for (Slot& slot : peer.slots) {
			if (slot.state != SlotState::pending) {
				continue;
			}

			if (slot.sync) {
				slot.state = SlotState::free;
				peer.inFlight--;
			} else {
				slot.state = SlotState::reset;
			}
		}

		ReportRestart();
	}


	// A counter reply names the boot value and the counter the receiver holds. It is for this sender when the boot value is its own and no
	// session of it is established, and when the held counter is not behind the sender's counter. The sender then drops the sync in flight
	// and starts its next session after the held counter, so that the receiver takes that sync as newer. An established sender ignores it.
	void ReceiveCounterReply(uint16 boot, uint8 counter) {
		if (peer.established || !peer.bootGiven || boot != peer.bootValue) {
			return;
		}

		uint8 current = peer.newSession ? peer.nextCounter : peer.counter;
		if (static_cast<uint8>(counter - current) >= 128) {
			return;
		}

		if constexpr (InOrder) {
			held.syncAcked = false;
		}

		for (Slot& slot : peer.slots) {
			if (slot.state == SlotState::pending && slot.sync) {
				slot.state = SlotState::free;
				peer.inFlight--;
			}
		}

		peer.nextCounter = static_cast<uint8>(counter + 1);
		peer.newSession = true;
		peer.needSync = true;
	}


	// Opens the receiving side's session: the boot value and counter of the sender's session. The base starts at the sync's number, whatever
	// it is, so the first data packet is the one after it. Nothing of the old session is kept.
	void OpenSession(uint16 boot, uint8 counter, uint16 sequence) {
		peer.open = true;
		peer.heldBoot = boot;
		peer.heldCounter = counter;
		peer.receiveTag = ReliableWire::TagOf(boot, counter);
		peer.base = sequence;
		peer.seen = 0;
		peer.owesResync = false;
		peer.owedHead = 0;
		peer.owedCount = 0;
		if constexpr (InOrder) {
			held.head = 0;
		}
	}


	// A sync of the sender's boot with a counter 1 to 127 ahead of the held one (serial arithmetic) opens its session. A sync of another
	// boot opens its session. When a session of the sender is held, the packets in flight of the sending side are reset here, at once,
	// and the restart runs. With no session held, nothing is reset here: the reset comes with the resync of the first refused packet. A
	// sync of the held counter is a copy of the held session's sync: it is acknowledged again. A sync of an older counter is stale: it is
	// ignored and not acknowledged. Returns true when the session was opened.
	bool ReceiveSync(uint16 boot, uint8 counter, uint16 sequence) {
		if (peer.open && boot == peer.heldBoot) {
			uint8 distance = static_cast<uint8>(counter - peer.heldCounter);
			if (distance == 0) {
				PushOwed(sequence, peer.receiveTag);
				return false;
			}

			if (distance >= 128) {
				peer.owesCounter = true;
				return false;
			}
		}

		if (peer.open) {
			if (boot != peer.heldBoot) {
				// The peer rebooted, so the session of our packets is gone at the peer too: they are reset here, at once.
				ResetSending();
			} else {
				ReportRestart();
			}
		}

		OpenSession(boot, counter, sequence);
		return true;
	}


	// Receiving side. Returns true when the packet is a data packet that goes up: inside the window, and not a duplicate. A sync is never
	// passed up. Each packet first moves the base to the sender's base when that is newer, so the window follows what the sender settled.
	// With InOrder a data packet inside the window is held instead (see HoldPacket), and it goes up from Process.
	bool ReceiveSequenced(uint8 tag, uint16 sequence, uint8 delta, bool sync, uint16 boot, uint8 counter,
		[[maybe_unused]] std::span<const uint8> payload) {
		uint16 mask = Mask();
		if (sync && !ReceiveSync(boot, counter, sequence)) {
			return false;
		}

		if (!peer.open || tag != peer.receiveTag) {
			peer.owesResync = true;
			peer.resyncTag = tag;
			return false;
		}

		uint16 senderBase = static_cast<uint16>((sequence - delta) & mask);
		uint16 moved = static_cast<uint16>((senderBase - peer.base) & mask);
		if (moved != 0 && moved < ((mask >> 1) + 1)) {
			MoveBase(senderBase);
		}

		uint16 ahead = static_cast<uint16>((sequence - peer.base) & mask);
		if (ahead < options.window && ahead < WindowCapacity) {
			if constexpr (InOrder) {
				if (!sync) {
					return HoldPacket(ahead, payload);
				}
			}

			PushOwed(sequence, tag);
			if constexpr (TracksAhead) {
				uint32 bit = 1u << ahead;
				if ((peer.seen & bit) != 0) {
					return false;
				}

				if (ahead == 0) {
					AdvanceBase();
				} else {
					peer.seen |= bit;
				}
			} else {
				AdvanceBase();
			}

			return !sync;
		}

		uint16 behind = static_cast<uint16>((peer.base - sequence) & mask);
		if (behind >= 1 && behind <= options.window) {
			PushOwed(sequence, tag);
		}

		return false;
	}


	// The base moves forward to target. The numbers below it are settled at the sender, so they are dropped from the window. With InOrder
	// the held packets below the target are dropped as well: the sender has given them up, so they are not acknowledged and do not go up.
	void MoveBase(uint16 target) {
		if constexpr (InOrder) {
			uint16 shift = static_cast<uint16>((target - peer.base) & Mask());
			peer.seen = shift >= 32 ? 0 : (peer.seen >> shift);
			peer.base = target;
			held.head = static_cast<uint8>((static_cast<std::size_t>(held.head) + shift) % WindowCapacity);
		} else if constexpr (TracksAhead) {
			uint16 shift = static_cast<uint16>((target - peer.base) & Mask());
			peer.seen = shift >= 32 ? 0 : (peer.seen >> shift);
			peer.base = target;
			if ((peer.seen & 1u) != 0) {
				AdvanceBase();
			}
		} else {
			peer.base = target;
		}
	}


	// The lowest number is received: the base moves past it, and past every following number that is already received.
	void AdvanceBase() {
		uint16 mask = Mask();
		peer.base = static_cast<uint16>((peer.base + 1) & mask);
		if constexpr (TracksAhead) {
			peer.seen >>= 1;
			while ((peer.seen & 1u) != 0) {
				peer.base = static_cast<uint16>((peer.base + 1) & mask);
				peer.seen >>= 1;
			}
		}

		if constexpr (InOrder) {
			held.head = static_cast<uint8>((held.head + 1) % WindowCapacity);
		}
	}


	// In order: keeps a data packet inside the window until the packets below it have gone up. It is acknowledged when it goes up, so an
	// acknowledgement means the packet was passed up. A copy of a held packet is not kept again. A payload larger than PayloadCapacity is
	// not kept and not acknowledged.
	bool HoldPacket(uint16 ahead, std::span<const uint8> payload) {
		uint32 bit = 1u << ahead;
		if (payload.size() > PayloadCapacity || (peer.seen & bit) != 0) {
			return false;
		}

		std::size_t slot = (static_cast<std::size_t>(held.head) + ahead) % WindowCapacity;
		ReliableWire::CopyBytes(payload.data(), payload.size(), held.payloads[slot].data());
		held.lengths[slot] = static_cast<uint16>(payload.size());
		peer.seen |= bit;
		return false;
	}


	// In order: the base moves past the held packet at the base, which has gone up. The packet and its slot are then free.
	void ShiftBase() {
		peer.base = static_cast<uint16>((peer.base + 1) & Mask());
		peer.seen >>= 1;
		held.head = static_cast<uint8>((held.head + 1) % WindowCapacity);
	}


	// In order: passes up the held packets from the base on, in sequence, while the base is held. Each one that goes up is acknowledged. A
	// packet the outbox refuses stays held, and the next Process tries it again. The base stops at the first number that is not held: the
	// sender's next packet moves the base past a gap it has given up. Returns false when the outbox refused a packet.
	bool ReleaseInOrder(const Outbox& outbox) {
		while ((peer.seen & 1u) != 0) {
			std::span<const uint8> payload(held.payloads[held.head].data(), held.lengths[held.head]);
			if (outbox.Deliver(payload) != ResultStatus::ok) {
				return false;
			}

			PushOwed(peer.base, peer.receiveTag);
			ShiftBase();
		}

		return true;
	}


	// Whether a packet numbered next would lie a window or more after an unsettled packet. The sender sends only inside one window, so
	// the receiver's window covers every packet in flight.
	bool SpanExceeded(uint16 next) const {
		uint16 mask = Mask();
		for (const Slot& slot : peer.slots) {
			if (slot.state == SlotState::pending && static_cast<uint16>((next - slot.sequence) & mask) >= options.window) {
				return true;
			}
		}

		return false;
	}


	// Reports the packets acknowledged or reset, and frees their slots. The handler runs ahead of the slot being freed, so the payload it
	// is given stays in place during the call.
	void SettleSlots() {
		for (Slot& slot : peer.slots) {
			if (slot.state == SlotState::acked) {
				Settle(slot, ResultStatus::ok);
			} else if (slot.state == SlotState::reset) {
				Settle(slot, ResultStatus::reset);
			}
		}
	}


	// A sync has no outcome: it carries no packet of the application.
	void Settle(Slot& slot, ResultStatus status) {
		if constexpr (HoldsHandler(options.outcomeFn)) {
			if (!slot.sync) {
				Outcome outcome{status, slot.sequence, slot.attempts,
					std::span<const uint8>(slot.bytes.data() + header, slot.length), OwnerValue()};
				(*options.outcomeFn)(outcome);
			}
		}

		slot.state = SlotState::free;
		peer.inFlight--;
	}


	// Starts the sync of a session that is starting, when there is a free slot. The first sync of a session takes its counter here. A sync
	// that is sent again keeps its counter. The sync is sent in this Process, through RunTimers, at once. The sync has the first number of
	// the boot, masked to the sequence width, and the data of the session starts after it. So a late acknowledgement of another boot's sync
	// does not name it.
	void StartSync() {
		if (!peer.needSync || SyncInFlight()) {
			return;
		}

		Slot* slot = FindFree();
		if (slot == nullptr) {
			return;
		}

		if (peer.newSession) {
			peer.counter = peer.nextCounter;
			peer.nextCounter = static_cast<uint8>(peer.nextCounter + 1);
			peer.tag = ReliableWire::TagOf(peer.bootValue, peer.counter);
			peer.newSession = false;
		}

		peer.needSync = false;
		uint16 number = static_cast<uint16>(ReliableWire::FirstOf(peer.bootValue) & Mask());
		peer.nextSequence = static_cast<uint16>((number + 1) & Mask());
		slot->sequence = number;
		slot->length = 0;
		slot->sync = true;
		slot->attempts = 0;
		slot->dueUs = lastNowUs;
		slot->state = SlotState::pending;
		peer.inFlight++;
	}


	// Sends the owed acknowledgements, each alone, and stops at the first one the link refuses.
	void FlushAcknowledgements(const Outbox& outbox) {
		uint16 ack = 0;
		uint8 ackTag = 0;
		while (PeekOwed(ack, ackTag)) {
			Fields fields{};
			fields.flags = static_cast<uint8>(ControlFlag | AcknowledgesFlag);
			fields.ackTag = ackTag;
			fields.ack = ack;
			std::array<uint8, header> head{};
			std::size_t length = PutHeader(head, fields);
			if (outbox.Send(std::span<const uint8>(head.data(), length)) == ResultStatus::busy) {
				return;
			}

			PopOwed();
		}
	}


	// Sends the resends that are due. A packet that has used its tries is reported as retryExhausted at its timeout. A sync that has used
	// its tries is dropped without an outcome: the next Send starts the session again, with the same counter. A data packet that is due is
	// first offered to the layer above when it declares DeliveryWanted: one it does not want is not sent, and its outcome is operationCancelled.
	void RunTimers(uint32 nowUs, const Outbox& outbox) {
		uint16 base = 0;
		if constexpr (resendsWanted) {
			base = SenderBase();
		}

		for (std::size_t index = 0; index < WindowCapacity; index++) {
			Slot* found = nullptr;
			if constexpr (resendsWanted) {
				// The packets are visited in the order of their numbers from the sender's base, the order they were given in.
				found = FindPending(static_cast<uint16>((base + index) & Mask()));
			} else {
				found = &peer.slots[index];
			}

			if (found == nullptr || found->state != SlotState::pending || !ReliableWire::Due(nowUs, found->dueUs)) {
				continue;
			}

			Slot& slot = *found;
			if (slot.attempts > options.retries) {
				if (slot.sync) {
					slot.state = SlotState::free;
					peer.inFlight--;
				} else {
					Settle(slot, ResultStatus::retryExhausted);
				}

				continue;
			}

			if constexpr (HoldsHandler(options.wantedFn)) {
				std::span<const uint8> payload(slot.bytes.data() + header, slot.length);
				if (!slot.sync && !(*options.wantedFn)(OwnerValue(), payload)) {
					Settle(slot, ResultStatus::operationCancelled);
					continue;
				}
			}

			if (!Transmit(slot, outbox)) {
				continue;
			}

			// The first send of a data packet is made in Add, so a send from here is a resend. The first send of a sync is made here, and is not
			// counted.
			if constexpr (options.counted) {
				if (slot.attempts > 0) {
					CountDrop(counts.resends);
				}
			}

			slot.attempts++;
			slot.dueUs = nowUs + TimeoutUs(slot.attempts);
		}
	}


	// Sends a slot's packet again, with its header rebuilt: an acknowledgement owed to the peer rides on it. Returns false when the
	// link's queue is full; the slot is then tried again in the next Process.
	bool Transmit(Slot& slot, const Outbox& outbox) {
		Fields fields{};
		fields.flags = static_cast<uint8>(SequencedFlag | (slot.sync ? SyncFlag : 0));
		fields.tag = peer.tag;
		fields.sequence = slot.sequence;
		fields.delta = BaseDelta(slot.sequence);
		if (slot.sync) {
			fields.boot = peer.bootValue;
			fields.counter = peer.counter;
		}

		uint16 ack = 0;
		uint8 ackTag = 0;
		bool piggyback = PeekOwed(ack, ackTag);
		if (piggyback) {
			fields.flags = static_cast<uint8>(fields.flags | AcknowledgesFlag);
			fields.ackTag = ackTag;
			fields.ack = ack;
		}

		std::array<uint8, header> head{};
		std::size_t headLength = PutHeader(head, fields);
		std::size_t at = header - headLength;
		ReliableWire::CopyBytes(head.data(), headLength, slot.bytes.data() + at);
		Meta meta{};
		if constexpr (resendsWanted) {
			meta.flags = Meta::ResentBelowFlag;
		}

		if (outbox.Send(std::span<const uint8>(slot.bytes.data() + at, headLength + slot.length), meta) == ResultStatus::busy) {
			return false;
		}

		if (piggyback) {
			PopOwed();
		}

		return true;
	}
};


// The layer with its default sizes: payloads up to 32 bytes, up to 4 packets in flight.
using Reliable = ReliableOf<32, 4>;

}
