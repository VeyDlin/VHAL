#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/BuiltIn.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Concurrent.h>
#include <Drivers/Interface/VUB/Requests/Events.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests/Requester.h>
#include <Drivers/Interface/VUB/Requests/Responder.h>
#include <Drivers/Interface/VUB/Requests/Ticket.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/WaitCondition/WaitCondition.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>


namespace VUB {


// The options of the requests layer. They are constants of the layer, given as template arguments by the Step methods, so a capacity that
// is off costs no storage. ownerBelow is set by the builder: it is true when the block below the layer is Reliable, which gives the outcomes
// and the restarts of the requests.
struct RequestsOptions {
	uint8 open = 0;
	uint8 tickets = 0;
	uint8 refusals = 4;
	bool ownerBelow = false;
	bool counted = false;
	bool ping = false;
	uint16 pingPadding = 0;
	uint32 builtInTimeoutMs = BuiltInWire::DefaultTimeoutMs;
	bool answerPing = false;
	uint16 answerPadding = 0;
	bool concurrent = false;
	uint8 pending = 4;
	bool awaitable = false;
	bool health = false;
	uint16 healthBytes = 0;
	bool answerHealth = false;
	uint16 answerHealthBytes = 0;
	void (* const* healthFn)(std::span<uint8>) = nullptr;
};


// The counters of a layer that is Counted(): the refusals and the Delivered events lost. Each one stops at 65535. The replies that match no
// open request are counted by the requester part.
struct RequestCounts {
	uint16 refused = 0;
	uint16 eventsLost = 0;
};


// The empty stand-in of RequestCounts for a layer that is not Counted().
struct NoRequestCounts {
};


// The index of the On entry whose id value is id in a tuple of handler entries, or the size of the tuple when there is none. The entries
// of a set are checked against its commands by CheckHandlers, so the value of the id is enough here.
template<typename Tuple, HandlerKind Kind, std::size_t... Index>
constexpr std::size_t FindKindIndexIn(uint8 id, std::index_sequence<Index...>) {
	constexpr std::array<bool, sizeof...(Index)> isKind = {(std::tuple_element_t<Index, Tuple>::kind == Kind)...};
	constexpr std::array<uint8, sizeof...(Index)> ids = {static_cast<uint8>(std::tuple_element_t<Index, Tuple>::key)...};
	for (std::size_t index = 0; index < sizeof...(Index); index++) {
		if (isKind[index] && ids[index] == id) {
			return index;
		}
	}

	return sizeof...(Index);
}


template<typename HandlerSet, HandlerKind Kind>
constexpr std::size_t FindHandlerOfKind(uint8 id) {
	using Tuple = typename HandlerSet::List;
	return FindKindIndexIn<Tuple, Kind>(id, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}


// For each command of the set, the index of its entry of Kind (On or OnCancel) in the handler list, or the list's size when it has none.
template<typename Commands, typename HandlerSet, HandlerKind Kind>
constexpr std::array<std::size_t, Commands::commandCount> BuildHandlerIndex() {
	std::array<std::size_t, Commands::commandCount> table{};
	for (std::size_t index = 0; index < Commands::commandCount; index++) {
		table[index] = FindHandlerOfKind<HandlerSet, Kind>(Commands::commandTable[index].id);
	}

	return table;
}


// True when a command with a reply has a handler in the list: only such a command takes a ticket.
template<typename Commands>
constexpr bool HasTicketHandler(const std::array<std::size_t, Commands::commandCount>& table, std::size_t handlerCount) {
	for (std::size_t index = 0; index < Commands::commandCount; index++) {
		if (table[index] < handlerCount && Commands::commandTable[index].level >= 3) {
			return true;
		}
	}

	return false;
}


// True when the handler list has an entry for a command (On or OnCancel). Only such a layer serves requests: an event handler alone builds
// no responder.
template<typename... Entry>
constexpr bool AnyCommandEntry(std::tuple<Entry...>) {
	return (false || ... || (Entry::kind != HandlerKind::onEvent));
}


// The shape of a layer with its options: which parts of the layer exist. Each flag is a constant, so a part that is off builds no code and
// takes no storage. The layer and its parts read the same plan.
template<typename Commands, typename HandlerSet, RequestsOptions Value>
struct RequestsPlan {
	static constexpr std::array<std::size_t, Commands::commandCount> onIndex = BuildHandlerIndex<Commands, HandlerSet, HandlerKind::on>();
	static constexpr std::array<std::size_t, Commands::commandCount> cancelIndex =
		BuildHandlerIndex<Commands, HandlerSet, HandlerKind::onCancel>();
	static constexpr std::size_t handlerCount = std::tuple_size_v<typename HandlerSet::List>;
	static constexpr bool served = AnyCommandEntry(typename HandlerSet::List{});
	static constexpr bool sendsPing = Value.ping;
	static constexpr bool asksHealth = Value.health;
	static constexpr bool sendsBuiltIn = sendsPing || asksHealth;
	static constexpr bool answerPing = Value.answerPing;
	static constexpr bool answerHealth = Value.answerHealth;
	static constexpr bool answers = served || answerPing || answerHealth;
	static constexpr bool ticketed = HasTicketHandler<Commands>(onIndex, handlerCount);
	static constexpr bool cancellable = AnyCommandFlag(Commands::commandTable, &CommandInfo::cancellable);
	// Interim slots exist only for interim commands. A cancel-refused interim (0xF8) for a command without one has no handler and is dropped.
	static constexpr bool anyInterim = Commands::hasInterim;
	static constexpr bool sends = Value.open > 0 || Commands::hasFire;
	static constexpr bool usesLink = Commands::hasFailWhileDown && sends;
	static constexpr std::size_t openCount = Value.open;
	static constexpr std::size_t ticketCount = ticketed ? Value.tickets : 0;
	static constexpr std::size_t refusalCount = answers ? Value.refusals : 0;
	static constexpr bool ownerBelow = Value.ownerBelow;
	// The requester keeps cancel state for its requests; the responder for its tickets, and answers a cancel of any of them.
	static constexpr bool requesterCancels = cancellable && openCount > 0;
	static constexpr bool responderCancels = cancellable && ticketed;
	static constexpr bool counted = Value.counted;
	static constexpr bool eventsReceived = AnyOnEvent(typename HandlerSet::List{});
	static constexpr bool countsLoss = counted && ownerBelow && AnyEventDelivered<Commands>();
	static constexpr uint16 pingPadding = Value.pingPadding;
	static constexpr uint32 builtInTimeoutMs = Value.builtInTimeoutMs;
	static constexpr uint16 answerPadding = Value.answerPadding;
	static constexpr bool concurrent = Value.concurrent;
	static constexpr bool awaitable = Value.awaitable && Value.concurrent;
	static constexpr bool eventsDeclared = Commands::eventCount > 0;
	// The packets of a concurrent layer that another task may hand to Process: its requests, cancels, fires, pings and events.
	static constexpr bool queued = Value.concurrent && (sends || eventsDeclared);
	static constexpr std::size_t pendingCount = Value.pending;
	// The user body sizes of the two roles: the asking side's size checks the handler's body (the report is decoded with the handler's type), and
	// the answering side's size is the body its fill writes.
	static constexpr uint16 healthBytes = Value.healthBytes;
	static constexpr uint16 answerHealthBytes = Value.answerHealthBytes;
	static constexpr void (* const* healthFill)(std::span<uint8>) = Value.healthFn;
	// The largest packet a requester slot sends for a built-in request, and the bytes of the one reply buffer of a responder.
	static constexpr std::size_t builtInRequestBytes = std::max(PingRequestBytes(sendsPing, pingPadding), HealthRequestBytes(asksHealth));
	static constexpr std::size_t replyBytes = BuiltInReplyBytes(answerPing, answerPadding, answerHealth, answerHealthBytes);
	// The largest packet of the layer: a queued packet of a concurrent layer is at most this long.
	static constexpr std::size_t largestPacket = std::max({
		RequestWire::RequestHeader + Commands::largestRequest,
		RequestWire::FinalHeader + Commands::largestReply,
		RequestWire::InterimSize,
		RequestWire::FinalErrorSize,
		RequestWire::EventHeader + Commands::largestEvent,
		RequestWire::CancelSize,
		BuiltInReplyBytes(Value.answerPing, Value.answerPadding, Value.answerHealth, Value.answerHealthBytes),
		PingRequestBytes(Value.ping, Value.pingPadding),
		HealthRequestBytes(Value.health || Value.answerHealth),
		HealthReplyBytes(Value.health, Value.healthBytes),
		HealthReplyBytes(Value.answerHealth, Value.answerHealthBytes)});
	static_assert(!sendsBuiltIn || (builtInTimeoutMs > 0 && builtInTimeoutMs <= BuiltInWire::MaxTimeoutMs),
		"BuiltInTimeoutMs must be from 1 to 2147483 milliseconds");
	static_assert(sendsBuiltIn || builtInTimeoutMs == BuiltInWire::DefaultTimeoutMs,
		"BuiltInTimeoutMs needs the Step .Ping(maxPadding) or .Health<U>() of the layer");
	static_assert(!asksHealth || openCount > 0, "Health needs requester slots: give the layer Open(n) with n of at least 1");
	static_assert(!answerHealth || counted, "AnswerHealth needs this layer's Counted(): add .Counted() to the requests layer");
	static_assert(!queued || Value.pending > 0, "Pending needs at least 1 packet: give the layer Pending(n) with n of at least 1");
};


// The grace an Await adds to the reply deadline of its request: the wait gives up after the request's own deadline has passed.
inline constexpr uint32 AwaitGraceMs = 250;


// The type of Await for a command: its final, or for a command with no reply the result of its delivery.
template<typename Commands, auto Id, bool NoReply = (CommandOf<Commands, Id>::level == 2)>
struct AwaitResultOf {
	using Type = Final<typename CommandOf<Commands, Id>::StatusType, typename CommandOf<Commands, Id>::ReplyType>;
};


template<typename Commands, auto Id>
struct AwaitResultOf<Commands, Id, true> {
	using Type = ResultStatus;
};


// The requests layer: requests with a typed body and one final reply, interim replies, and the handlers of the commands it serves. The
// command set and the handler list are template arguments; the options are the Step methods Open(n) (requester slots), Tickets(n)
// (responder tickets) and Refusals(n) (the refusal ring). A layer whose options are 0 builds no code for them.
//
// The layer composes two parts: the requester (Requests/Requester.h) for the requests it sends, and the responder (Requests/Responder.h) for
// the requests it serves. Each part is a member of its own, and a part that is off has no storage. The layer takes the packets and calls
// the parts in order from Take and Process.
//
// The layer sits above a block that takes the owner (a Reliable layer), or above a block that does not: with Reliable below (ownerBelow),
// its requests get the outcomes of their packets and the restarts of the peer. Without it a request ends with its final, its deadline, or
// the connection's loss; a command with no reply is sent with Fire, which has no delivery at all. The handler of a request runs from Process,
// and a final made in a handler goes out in the same Process. The handlers of a final, an interim, and a command with no reply run where the
// packet is taken, so a handler that sends a request sends it at once.
template<typename Commands, typename HandlerSet, RequestsOptions Value = RequestsOptions{}>
class RequestsOf {
public:
	using Options = RequestsOptions;
	using Plan = RequestsPlan<Commands, HandlerSet, Value>;
	using EventPart = EventsOf<Commands, HandlerSet, Plan>;

	template<Options value>
	using Configured = RequestsOf<Commands, HandlerSet, value>;


	// The options of the layer, after the layer in the chain: each method reads them with LastOptions, changes one field, and returns the
	// Step over WithLastOptions.
	template<typename Builder>
	struct Step : Builder {
		explicit constexpr Step(const Builder& base) : Builder(base) {
		}


		// The requester slots: the number of requests that may be open at once. 0 builds no slot.
		constexpr Step Open(uint8 count) const {
			Options next = this->LastOptions();
			next.open = count;
			return Step(this->WithLastOptions(next));
		}


		// The responder tickets: the number of requests the layer may hold for its handlers at once.
		constexpr Step Tickets(uint8 count) const {
			Options next = this->LastOptions();
			next.tickets = count;
			return Step(this->WithLastOptions(next));
		}


		// The refusal ring: the refusals waiting to be sent from Process.
		constexpr Step Refusals(uint8 count) const {
			Options next = this->LastOptions();
			next.refusals = count;
			return Step(this->WithLastOptions(next));
		}


		// The counters: refused, lateReplies and eventsLost, read through Refused(), LateReplies() and EventsLost(). Without this call
		// the layer keeps no counter and builds no code that counts.
		constexpr Step Counted() const {
			Options next = this->LastOptions();
			next.counted = true;
			return Step(this->WithLastOptions(next));
		}


		// The built-in ping, sending side: the layer sends pings from its requester slots, so Open(n) of at least 1 is needed. maxPadding is the
		// largest padding a ping may carry; a ping with more returns messageTooLong.
		constexpr Step Ping(uint16 maxPadding) const {
			Options next = this->LastOptions();
			next.ping = true;
			next.pingPadding = maxPadding;
			return Step(this->WithLastOptions(next));
		}


		// The built-in ping, answering side: the layer answers the pings it receives. maxPadding is the largest padding of a reply. The layer
		// needs no Open, no handler and no ticket for it.
		constexpr Step AnswerPing(uint16 maxPadding) const {
			Options next = this->LastOptions();
			next.answerPing = true;
			next.answerPadding = maxPadding;
			return Step(this->WithLastOptions(next));
		}


		// The built-in health request, sending side: the layer sends health requests from its requester slots, so Open(n) of at least 1 is
		// needed. U is the user body of the reply, VUB::NoBody when the responder has none. The handler of Health<&Handler>() takes a
		// HealthReport<U>.
		template<typename U = NoBody>
		constexpr Step Health() const {
			Options next = this->LastOptions();
			next.health = true;
			next.healthBytes = static_cast<uint16>(BodyViewOf<U>.maxSize);
			return Step(this->WithLastOptions(next));
		}


		// The built-in health request, answering side: the layer answers each health request it takes. Fill is void (*)(U&) and writes the user
		// body; it is called once per request, from Process. Needs Counted() on the layer. The layer needs no Open, no handler and no ticket for
		// it.
		template<typename U, void (*Fill)(U&)>
		constexpr Step AnswerHealth() const {
			Options next = this->LastOptions();
			next.answerHealth = true;
			next.answerHealthBytes = static_cast<uint16>(BodyViewOf<U>.maxSize);
			next.healthFn = &HealthFillHandler<&EncodeHealthBody<U, Fill>>;
			return Step(this->WithLastOptions(next));
		}


		// The deadline of a built-in request, in milliseconds: the time a ping or a health request waits for its reply. The default is 1000.
		constexpr Step BuiltInTimeoutMs(uint32 ms) const {
			Options next = this->LastOptions();
			next.builtInTimeoutMs = ms;
			return Step(this->WithLastOptions(next));
		}


		// The packets a concurrent layer holds for Process: the requests, cancels, fires, pings and events that another task sends. A full
		// queue returns busy. The storage is one packet of the largest size per count, and exists only on a concurrent stack. The default is 4.
		constexpr Step Pending(uint8 count) const {
			Options next = this->LastOptions();
			next.pending = count;
			return Step(this->WithLastOptions(next));
		}


		// The waits of Await: the requests of this layer may be awaited by a task. Await needs a concurrent stack as well.
		constexpr Step Awaitable() const {
			Options next = this->LastOptions();
			next.awaitable = true;
			return Step(this->WithLastOptions(next));
		}
	};


	// The requests this layer gives the application: its handle is built by the stack for the layer's position.
	class Handle {
	public:
		Handle(RequestsOf& owner, Outbox sender) : layer(&owner), outbox(sender) {
		}


		// A request of a command with a reply: the body is sent, the final goes to OnFinal and the interim replies to OnInterim (both
		// optional). A busy refusal is returned at once, with no number used. Without Reliable below, noInit is returned until SetSession.
		template<auto Id, auto OnFinal, auto OnInterim = nullptr>
		Result<RequestId> Send(const typename CommandOf<Commands, Id>::BodyType& body) const {
			return layer->template StartRequest<Id, OnFinal, OnInterim>(body, outbox);
		}


		// The same for a command with no body: a request of it, with its final and interim handlers (both optional).
		template<auto Id, auto OnFinal, auto OnInterim = nullptr>
		Result<RequestId> Send() const requires (std::is_same_v<typename CommandOf<Commands, Id>::BodyType, NoBody>) {
			return Send<Id, OnFinal, OnInterim>(NoBody{});
		}


		// A request of a command with no reply: the packet goes without a slot and without an outcome.
		template<auto Id>
		ResultStatus Fire(const typename CommandOf<Commands, Id>::BodyType& body) const {
			return layer->template FireRequest<Id>(body, outbox);
		}


		// The same for a command with no body.
		template<auto Id>
		ResultStatus Fire() const requires (std::is_same_v<typename CommandOf<Commands, Id>::BodyType, NoBody>) {
			return Fire<Id>(NoBody{});
		}


		// The cancel of an open request of a command that is Cancellable(). The CANCEL is sent at once when the request can take it, and
		// otherwise held until its REQUEST is acknowledged. Returns ok (also for a second cancel), or notFound when no open request has the id.
		template<auto Id>
		ResultStatus Cancel(RequestId id) const {
			static_assert(CommandInfoOf<Commands, Id>().cancellable, "Cancel names a command that is not Cancellable(): add .Cancellable() after its Reply");
			return layer->template CancelRequest<Id>(id.number, outbox);
		}


		// The cancel of every open request of a cancellable command. Each request gets its own CANCEL, sent or held as Cancel does.
		ResultStatus CancelAll() const {
			static_assert(Plan::cancellable, "CancelAll needs a command that is Cancellable(): add .Cancellable() after a Reply");
			return layer->CancelAllRequests(outbox);
		}


		// An event of the set: its packet goes out through the outbox, unacknowledged unless the event is Delivered. Returns what the outbox returns.
		// On a concurrent layer the packet is queued for Process, and the call returns ok or busy.
		template<auto Id>
		ResultStatus Emit(const typename EventOf<Commands, Id>::BodyType& body) const {
			if constexpr (Value.concurrent) {
				return layer->template EmitQueued<Id>(body);
			} else {
				return EventPart::template Emit<Id>(body, outbox);
			}
		}


		// The same for an event with no body.
		template<auto Id>
		ResultStatus Emit() const requires (std::is_same_v<typename EventOf<Commands, Id>::BodyType, NoBody>) {
			return Emit<Id>(NoBody{});
		}


		// A request of a command with a reply, awaited: the call sends it and waits until its final is in. The result is the final, with its
		// status and body; a refused send is returned in result, and a wait that reaches its limit gives timeout. The request's final handler
		// does not run. A command with no reply returns its result. Needs the Step .Awaitable() and a concurrent stack.
		template<auto Id, auto Interim = nullptr>
		typename AwaitResultOf<Commands, Id>::Type Await(const typename CommandOf<Commands, Id>::BodyType& body) const {
			static_assert(Value.awaitable, "Await needs the requests layer's Step .Awaitable(): add .Awaitable() to the requests layer");
			static_assert(Value.concurrent, "Await needs a concurrent stack: add .Concurrent() to the builder");
			return layer->template AwaitRequest<Id, Interim>(body, outbox);
		}


		// The same for a command with no body.
		template<auto Id, auto Interim = nullptr>
		typename AwaitResultOf<Commands, Id>::Type Await() const requires (std::is_same_v<typename CommandOf<Commands, Id>::BodyType, NoBody>) {
			return Await<Id, Interim>(NoBody{});
		}


		// The session of a layer with no Reliable below: the first request number and tag (see RequesterOf::SetSession). Not a member otherwise.
		void SetSession(uint16 value) const requires (!Value.ownerBelow) {
			layer->SetSession(value);
		}


		// The counters of the layer (see Step::Counted). Not members of a layer that is not Counted.
		uint16 Refused() const requires (Value.counted) {
			return layer->Refused();
		}


		uint16 LateReplies() const requires (Value.counted) {
			return layer->LateReplies();
		}


		uint16 EventsLost() const requires (Value.counted) {
			return layer->EventsLost();
		}


		// A ping to the peer: the handler gets the result and the round trip. The request is sent by the next Process. Padding above the
		// layer's maximum returns messageTooLong. Only a layer with the Step .Ping(maxPadding) has it.
		template<auto Handler>
		ResultStatus Ping(uint16 padding = 0) const {
			return layer->template PingRequest<Handler>(padding);
		}


		// A health request to the peer: the handler gets the peer's HealthReport<U>. The request is sent by the next Process, with the same
		// session, slot and deadline rules as Ping. Only a layer with the Step .Health<U>() has it.
		template<auto Handler>
		ResultStatus Health() const {
			return layer->template HealthRequest<Handler>();
		}


		RequestsOf& Owner() const {
			return *layer;
		}


	private:
		RequestsOf* layer;
		Outbox outbox;
	};


	static constexpr std::size_t header = 0;
	static constexpr uint32 fingerprint = 0x52455131;
	static constexpr bool usesTime = true;
	static constexpr bool makesBulk = false;
	static constexpr bool requestsLayer = true;
	static constexpr bool wantsOwner = true;
	static constexpr bool ownerOptional = true;
	static constexpr bool usesLinkState = Commands::hasFailWhileDown;
	// A layer that answers health requests reads the stack's counters through the CountingOutbox its Process takes.
	static constexpr bool usesCounters = Plan::answerHealth;
	// A layer that sends the counters on needs the stack's high-water marks, and Reliable's counters below it (checked by the stack).
	static constexpr bool needsCounters = Plan::answerHealth;
	static constexpr std::size_t largestPacket = Plan::largestPacket;


private:
	static_assert(CheckHandlers<Commands, HandlerSet>(), "the handler list does not match the command set");
	[[no_unique_address]] RequesterOf<Commands, Plan> requester{};
	[[no_unique_address]] std::conditional_t<Plan::answers, ResponderOf<Commands, HandlerSet, Plan>, NoStorage> responder{};
	[[no_unique_address]] std::conditional_t<Value.counted, RequestCounts, NoRequestCounts> counts{};


public:
	// The refusals, the replies that matched no open request, and the Delivered events lost. Each reads a counter of a layer that is Counted().
	uint16 Refused() const requires (Value.counted) {
		return counts.refused;
	}


	uint16 LateReplies() const requires (Value.counted) {
		return requester.LateReplies();
	}


	uint16 EventsLost() const requires (Value.counted) {
		return counts.eventsLost;
	}


	// The owner of Reliable's outcomes: a REQUEST's outcome settles its request, and the outcome of a Delivered event counts a loss. The other
	// kinds of packet have no outcome here. With no Reliable below there are no outcomes, and the function does nothing.
	static void DeliveryOutcome(const Outcome& outcome) {
		if constexpr (Value.ownerBelow && (Value.open > 0 || Plan::countsLoss)) {
			RequestsOf* self = static_cast<RequestsOf*>(outcome.owner);
			if constexpr (Value.open > 0) {
				self->requester.OnOutcome(outcome);
			}

			if constexpr (Plan::countsLoss) {
				EventPart::OnOutcome(outcome, self->counts.eventsLost);
			}
		} else {
			(void)outcome;
		}
	}


	// The peer restarted: every open request is closed with reset, and every ticket of the peer is closed (its final is discarded).
	static void DeliveryRestart(void* owner) {
		if constexpr (Value.ownerBelow) {
			RequestsOf* self = static_cast<RequestsOf*>(owner);
			self->OnPeerRestart();
		} else {
			(void)owner;
		}
	}


	// Reliable asks whether a packet is still wanted when it is due to be sent again. A REQUEST is wanted while its requester slot is open:
	// a request that its requester has closed (by its deadline, or by the link's loss) is not sent again. A CANCEL is wanted while its request is
	// open. Every other packet is wanted.
	static bool DeliveryWanted(void* owner, std::span<const uint8> payload) requires (Value.open > 0) {
		const RequestsOf* self = static_cast<const RequestsOf*>(owner);
		if constexpr (Plan::requesterCancels) {
			if (payload.size() >= RequestWire::CancelSize && payload[0] == RequestWire::KindCancel) {
				return self->requester.IsOpen(GetLe16(payload.data() + 1), payload[3]);
			}
		}

		if (payload.size() < RequestWire::RequestHeader || payload[0] != RequestWire::KindRequest) {
			return true;
		}

		return self->requester.IsOpen(GetLe16(payload.data() + 2), payload[4]);
	}


	// The layer's packets are not given to Add: they go out through the handle's outbox, below the layer.
	Result<std::size_t> Add(const Meta&, std::span<const uint8>, std::span<uint8>) {
		// The layer's packets go out through its handle, which enters below the layer; a packet given to Stack::Send is not a request.
		return ResultStatus::notSupported;
	}


	// Takes a packet by its kind: each kind has one branch, and the part that owns the kind takes the packet.
	Result<Taken> Take(const Meta&, std::span<const uint8> packet) {
		if (packet.empty()) {
			return ResultStatus::invalidData;
		}

		if (packet[0] == RequestWire::KindRequest) {
			return TakeRequest(packet);
		}

		if (packet[0] == RequestWire::KindInterim) {
			return TakeInterim(packet);
		}

		if (packet[0] == RequestWire::KindFinal) {
			return TakeFinal(packet);
		}

		if constexpr (Plan::cancellable) {
			if (packet[0] == RequestWire::KindCancel) {
				return TakeCancel(packet);
			}
		}

		if constexpr (Plan::eventsReceived) {
			if (packet[0] == RequestWire::KindEvent) {
				return TakeEvent(packet);
			}
		}

		return ResultStatus::invalidData;
	}


	// Runs once per Process: the requester's deadlines and the finals of the requests that closed without a packet, then the responder's
	// handlers, deadlines and packets. The handlers run here, so a request or a final made in a handler goes out in this Process.
	// The outbox is a CountingOutbox when the layer answers health requests (usesCounters), since the reply carries the stack's counters.
	template<typename OutboxType>
	void Process(uint32 nowUs, const OutboxType& outbox) {
		static_assert(!Plan::answerHealth || std::is_same_v<OutboxType, CountingOutbox>,
			"a layer that answers health requests is given the CountingOutbox of the stack");
		if constexpr (Plan::openCount > 0) {
			requester.Arm(nowUs);
			if constexpr (Plan::sendsBuiltIn) {
				requester.SendBuiltIns(nowUs, outbox);
			}

			requester.Expire(nowUs);
			requester.SendHeldCancels(outbox);
		}

		if constexpr (Plan::answers) {
			responder.RunReceived();
			responder.RunCancels();
			responder.RearmTickets(nowUs);
			responder.ExpireTickets(nowUs);
		}

		if constexpr (Plan::openCount > 0) {
			requester.Deliver(nowUs);
		}

		if constexpr (Plan::answerHealth) {
			responder.FillHealth(outbox.Counters(), counts.refused, requester.LateReplies(), counts.eventsLost);
		}

		if constexpr (Plan::answers) {
			responder.SendResponses(outbox);
		}

		// The packets of the handlers run in this Process: the drain comes after them, and the oldest packet goes first.
		if constexpr (Plan::queued) {
			requester.DrainOutgoing(outbox);
		}
	}


	// The connection changed. While it is down a request of a FailWhileDown command is closed with noConnection; the others wait for it, and
	// their deadlines run. The layer has this function when a command is FailWhileDown; its state is kept only when the layer sends.
	void OnLink(bool up, uint32) requires (Commands::hasFailWhileDown) {
		requester.OnLink(up);
	}


	// The requester's send and its fire, through the layer: the handle calls them. The requester part does the work.
	template<auto Id, auto OnFinal, auto OnInterim>
	Result<RequestId> StartRequest(const typename CommandOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		return requester.template Start<Id, OnFinal, OnInterim>(body, outbox);
	}


	template<auto Id>
	ResultStatus FireRequest(const typename CommandOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		return requester.template Fire<Id>(body, outbox);
	}


	// The event of a concurrent layer: the packet is queued for Process. The queue is the requester's, and ok or busy is what it returns.
	template<auto Id>
	ResultStatus EmitQueued(const typename EventOf<Commands, Id>::BodyType& body) {
		return EventPart::template EmitWith<Id>(body, [this](std::span<const uint8> packet, const Meta& meta) -> ResultStatus {
			return requester.Enqueue(packet, meta.flags);
		});
	}


	// The await of a request: the request is sent with a waiter on this task's stack, and the task waits until the waiter is done or its limit
	// passes. The limit is the command's reply deadline plus a grace of 250 ms, so the request's own deadline ends the wait first. A command with
	// no reply has no deadline of its own: its wait ends with the outcome of the delivery.
	template<auto Id, auto Interim>
	typename AwaitResultOf<Commands, Id>::Type AwaitRequest(const typename CommandOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		using CommandType = CommandOf<Commands, Id>;
		constexpr uint32 replyMs = CommandInfoOf<Commands, Id>().replyTimeoutMs;
		constexpr uint32 limitMs = replyMs > 0 ? replyMs + AwaitGraceMs : 0xFFFFFFFFu;
		if constexpr (CommandType::level == 2) {
			AwaitWaiter waiter{};
			waiter.deliver = &RequesterOf<Commands, Plan>::template DeliverAwait<Id>;
			Result<RequestId> sent = requester.template StartAwaited<Id, Interim>(body, outbox, &waiter);
			if (sent.IsErr()) {
				return sent.Error();
			}

			if (WaitAwaited(waiter, limitMs)) {
				waiter.result = ResultStatus::timeout;
			}

			return waiter.result;
		} else {
			using FinalType = typename AwaitResultOf<Commands, Id>::Type;
			AwaitWaiterOf<FinalType> waiter{};
			waiter.deliver = &RequesterOf<Commands, Plan>::template DeliverAwait<Id>;
			Result<RequestId> sent = requester.template StartAwaited<Id, Interim>(body, outbox, &waiter);
			if (sent.IsErr()) {
				FinalType refused{};
				refused.result = sent.Error();
				return refused;
			}

			if (WaitAwaited(waiter, limitMs)) {
				waiter.value.result = ResultStatus::timeout;
			}

			return waiter.value;
		}
	}


	// Waits until the final is in the waiter or the limit passes. Returns true when the wait was abandoned: the requester then clears the pointer
	// of the request to the waiter, so a final that arrives after the abandon is not written into this task's stack. A final written first is kept.
	bool WaitAwaited(AwaitWaiter& waiter, uint32 limitMs) {
		ResultStatus status = WaitCondition::WaitFor<WaitCondition::RtosYield>([this, &waiter]() {
			return requester.AwaitDone(waiter);
		}, limitMs);
		return status != ResultStatus::ok && requester.Abandon(waiter);
	}


	// The cancel calls of the handle: the requester does the work.
	template<auto Id>
	ResultStatus CancelRequest(uint16 number, const Outbox& outbox) {
		return requester.template Cancel<Id>(number, outbox);
	}


	ResultStatus CancelAllRequests(const Outbox& outbox) {
		requester.CancelAll(outbox);
		return ResultStatus::ok;
	}


	template<auto Handler>
	ResultStatus PingRequest(uint16 padding) {
		return requester.template Ping<Handler>(padding);
	}


	template<auto Handler>
	ResultStatus HealthRequest() {
		return requester.template Health<Handler>();
	}


	// The session of a layer with no Reliable below: the requester's first number and tag. Not a member when Reliable is below.
	void SetSession(uint16 value) requires (!Value.ownerBelow) {
		requester.SetSession(value);
	}


private:
	// The restart: the requester's open requests close with reset, and the responder's tickets and refusals are dropped.
	void OnPeerRestart() {
		if constexpr (Plan::openCount > 0) {
			requester.OnRestart();
		}

		if constexpr (Plan::answers) {
			responder.OnRestart();
		}
	}


	// Takes a FINAL of one of this layer's requests: the requester closes the request and calls its handler. A FINAL of code ok has a status
	// byte; without it the packet is refused, and the request it names stays open.
	Result<Taken> TakeFinal(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::FinalErrorSize || (packet[4] == RequestWire::CodeOk && packet.size() < RequestWire::FinalHeader)) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::openCount > 0) {
			requester.TakeFinal(packet);
		} else {
			requester.CountLate();
		}

		return Taken{0, false};
	}


	// Takes an INTERIM of an open request. The requester checks the index and the status, and calls the handler at once.
	Result<Taken> TakeInterim(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::InterimSize) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::openCount > 0 && Plan::anyInterim) {
			ResultStatus status = requester.TakeInterim(packet);
			if (status != ResultStatus::ok) {
				return status;
			}
		} else {
			requester.CountLate();
		}

		return Taken{0, false};
	}


	// Takes an EVENT, on a layer with an OnEvent handler. The requests received earlier in this walk run first, so the handler runs in arrival
	// order. An event with no handler, or an id the set does not have, is refused and counted in refused. A short body is invalidData, and the
	// packet is counted in layerError. A layer with no OnEvent handler takes no event: its EVENT packets are invalidData.
	Result<Taken> TakeEvent(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::EventHeader) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::served) {
			responder.RunReceived();
		}

		ResultStatus status = EventPart::Take(packet);
		if (status == ResultStatus::notFound) {
			if constexpr (Value.counted) {
				CountDrop(counts.refused);
			}

			return Taken{0, false};
		}

		if (status != ResultStatus::ok) {
			return status;
		}

		return Taken{0, false};
	}


	// Takes a REQUEST, the responder's side. A request with a reply takes a ticket, or is refused from the ring; a command with no reply runs at
	// once, after the requests already received in this walk. The packet is consumed in every case that is not an error.
	Result<Taken> TakeRequest(std::span<const uint8> packet) {
		if constexpr (!Plan::answers) {
			(void)packet;
			return ResultStatus::notSupported;
		} else {
			static_assert(!Plan::ticketed || Value.tickets > 0, "the layer serves a command and has no tickets: give Tickets(n) with n of at least 1");
			ResultStatus status = responder.Take(packet);
			if constexpr (Value.counted) {
				if (status == ResultStatus::busy) {
					CountDrop(counts.refused);
					return Taken{0, false};
				}
			}

			if (status != ResultStatus::ok) {
				return status;
			}

			return Taken{0, false};
		}
	}


	// Takes a CANCEL, the responder's side. The packet is consumed unless it is malformed.
	Result<Taken> TakeCancel(std::span<const uint8> packet) {
		if constexpr (!Plan::served) {
			(void)packet;
			return ResultStatus::notSupported;
		} else {
			ResultStatus status = responder.TakeCancel(packet);
			if (status != ResultStatus::ok) {
				return status;
			}

			return Taken{0, false};
		}
	}
};


// The requests layer with its command set and handler list. Requests is the layer with default options.
template<typename Commands, typename HandlerSet>
using Requests = RequestsOf<Commands, HandlerSet>;


}
