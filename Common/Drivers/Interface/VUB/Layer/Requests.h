#pragma once
#include <Drivers/Interface/VUB/Blocks.h>
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Require.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests/Requester.h>
#include <Drivers/Interface/VUB/Requests/Responder.h>
#include <Drivers/Interface/VUB/Requests/Ticket.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
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
};


// The index of the On entry whose id value is id in a tuple of handler entries, or the size of the tuple when there is none. The entries
// of a set are checked against its commands by CheckHandlers, so the value of the id is enough here.
template<typename Tuple, std::size_t... Index>
constexpr std::size_t FindOnIndexIn(uint8 id, std::index_sequence<Index...>) {
	constexpr std::array<bool, sizeof...(Index)> isOn = {(std::tuple_element_t<Index, Tuple>::kind == HandlerKind::on)...};
	constexpr std::array<uint8, sizeof...(Index)> ids = {static_cast<uint8>(std::tuple_element_t<Index, Tuple>::key)...};
	for (std::size_t index = 0; index < sizeof...(Index); index++) {
		if (isOn[index] && ids[index] == id) {
			return index;
		}
	}

	return sizeof...(Index);
}


template<typename HandlerSet>
constexpr std::size_t FindHandlerOn(uint8 id) {
	using Tuple = typename HandlerSet::List;
	return FindOnIndexIn<Tuple>(id, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}


// For each command of the set, the index of its On entry in the handler list, or the list's size when the command has none.
template<typename Commands, typename HandlerSet>
constexpr std::array<std::size_t, Commands::commandCount> BuildOnIndex() {
	std::array<std::size_t, Commands::commandCount> table{};
	for (std::size_t index = 0; index < Commands::commandCount; index++) {
		table[index] = FindHandlerOn<HandlerSet>(Commands::commandTable[index].id);
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


// The shape of a layer with its options: which parts of the layer exist. Each flag is a constant, so a part that is off builds no code and
// takes no storage. The layer and its parts read the same plan.
template<typename Commands, typename HandlerSet, RequestsOptions Value>
struct RequestsPlan {
	static constexpr std::array<std::size_t, Commands::commandCount> onIndex = BuildOnIndex<Commands, HandlerSet>();
	static constexpr std::size_t handlerCount = std::tuple_size_v<typename HandlerSet::List>;
	static constexpr bool served = handlerCount > 0;
	static constexpr bool ticketed = HasTicketHandler<Commands>(onIndex, handlerCount);
	static constexpr bool anyInterim = Commands::hasInterim;
	static constexpr bool sends = Value.open > 0 || Commands::hasFire;
	static constexpr bool usesLink = Commands::hasFailWhileDown && sends;
	static constexpr std::size_t openCount = Value.open;
	static constexpr std::size_t ticketCount = ticketed ? Value.tickets : 0;
	static constexpr std::size_t refusalCount = served ? Value.refusals : 0;
	static constexpr bool ownerBelow = Value.ownerBelow;
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


		// A request of a command with no reply: the packet goes without a slot and without an outcome.
		template<auto Id>
		ResultStatus Fire(const typename CommandOf<Commands, Id>::BodyType& body) const {
			return layer->template FireRequest<Id>(body, outbox);
		}


		// The session of a layer with no Reliable below: the first request number and tag (see RequesterOf::SetSession). Not a member otherwise.
		void SetSession(uint16 value) const requires (!Value.ownerBelow) {
			layer->SetSession(value);
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
	static constexpr std::size_t largestPacket = std::max({
		RequestWire::RequestHeader + Commands::largestRequest,
		RequestWire::FinalHeader + Commands::largestReply,
		RequestWire::InterimSize,
		RequestWire::FinalErrorSize});


private:
	static_assert(CheckHandlers<Commands, HandlerSet>(), "the handler list does not match the command set");
	[[no_unique_address]] RequesterOf<Commands, Plan> requester{};
	[[no_unique_address]] std::conditional_t<Plan::served, ResponderOf<Commands, HandlerSet, Plan>, NoStorage> responder{};


public:
	// The owner of Reliable's outcomes: a REQUEST's outcome settles its request. The other kinds of packet have no outcome here. With no
	// Reliable below there are no outcomes, and the function does nothing.
	static void DeliveryOutcome(const Outcome& outcome) {
		if constexpr (Value.ownerBelow && Value.open > 0) {
			RequestsOf* self = static_cast<RequestsOf*>(outcome.owner);
			self->requester.OnOutcome(outcome);
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
	// a request that its requester has closed (by its deadline, or by the link's loss) is not sent again. Every other packet is wanted.
	static bool DeliveryWanted(void* owner, std::span<const uint8> payload) requires (Value.open > 0) {
		if (payload.size() < RequestWire::RequestHeader || payload[0] != RequestWire::KindRequest) {
			return true;
		}

		const RequestsOf* self = static_cast<const RequestsOf*>(owner);
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

		return ResultStatus::invalidData;
	}


	// Runs once per Process: the requester's deadlines and the finals of the requests that closed without a packet, then the responder's
	// handlers, deadlines and packets. The handlers run here, so a request or a final made in a handler goes out in this Process.
	void Process(uint32 nowUs, const Outbox& outbox) {
		if constexpr (Plan::openCount > 0) {
			requester.Arm(nowUs);
			requester.Expire(nowUs);
		}

		if constexpr (Plan::served) {
			responder.RunReceived();
			responder.RearmTickets(nowUs);
			responder.ExpireTickets(nowUs);
		}

		if constexpr (Plan::openCount > 0) {
			requester.Deliver();
		}

		if constexpr (Plan::served) {
			responder.SendResponses(outbox);
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

		if constexpr (Plan::served) {
			responder.OnRestart();
		}
	}


	// Takes a FINAL of one of this layer's requests: the requester closes the request and calls its handler.
	Result<Taken> TakeFinal(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::FinalErrorSize) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::openCount > 0) {
			requester.TakeFinal(packet);
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
		}

		return Taken{0, false};
	}


	// Takes a REQUEST, the responder's side. A request with a reply takes a ticket, or is refused from the ring; a command with no reply runs at
	// once, after the requests already received in this walk. The packet is consumed in every case that is not an error.
	Result<Taken> TakeRequest(std::span<const uint8> packet) {
		if constexpr (!Plan::served) {
			(void)packet;
			return ResultStatus::notSupported;
		} else {
			static_assert(!Plan::ticketed || Value.tickets > 0, "the layer serves a command and has no tickets: give Tickets(n) with n of at least 1");
			ResultStatus status = responder.Take(packet);
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
