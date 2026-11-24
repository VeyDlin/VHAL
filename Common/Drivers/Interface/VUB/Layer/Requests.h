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
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


namespace VUB {


// The wire values of the requests layer. Kinds are byte 0 of a packet; codes are the FINAL code byte (design section 5). The layer's
// packets are the payloads of the layer below: a request is 01 id number(2) tag(1) body, an interim is 02 number(2) tag(1) index(1)
// status(1), and a final is 03 number(2) tag(1) code(1), with status(1) and body after code 0.
namespace RequestWire {
	inline constexpr uint8 KindRequest = 0x01;
	inline constexpr uint8 KindInterim = 0x02;
	inline constexpr uint8 KindFinal = 0x03;
	inline constexpr std::size_t RequestHeader = 5;
	inline constexpr std::size_t InterimSize = 6;
	inline constexpr std::size_t FinalHeader = 6;
	inline constexpr std::size_t FinalErrorSize = 5;
	inline constexpr uint8 CodeOk = 0;
	inline constexpr uint8 CodeCancelled = 1;
	inline constexpr uint8 CodeNotSupported = 2;
	inline constexpr uint8 CodeInvalidData = 3;
	inline constexpr uint8 CodeBusy = 4;
	inline constexpr uint8 CodeTimeout = 5;
	inline constexpr uint8 CodeInvalidParameter = 6;
	inline constexpr uint8 StatusReservedFrom = 0xF0;


	// The result of a final code. Codes 7 to 255 are not used by version 1, and a requester refuses them as invalidData.
	inline ResultStatus ResultOfCode(uint8 code) {
		if (code == CodeCancelled) {
			return ResultStatus::operationCancelled;
		}

		if (code == CodeNotSupported) {
			return ResultStatus::notSupported;
		}

		if (code == CodeBusy) {
			return ResultStatus::busy;
		}

		if (code == CodeTimeout) {
			return ResultStatus::timeout;
		}

		if (code == CodeInvalidParameter) {
			return ResultStatus::invalidParameter;
		}

		return ResultStatus::invalidData;
	}


	// True when the clock has reached due. The difference is a signed 32-bit count of microseconds, so a clock that wraps still compares right.
	inline bool Due(uint32 nowUs, uint32 dueUs) {
		return static_cast<int32>(nowUs - dueUs) >= 0;
	}
}


// The options of the requests layer. They are constants of the layer, given as template arguments by the Step methods, so a capacity that
// is off costs no storage. ownerBelow is set by the builder: it is true when the block below the layer is Reliable, which gives the outcomes
// and the restarts of the requests.
struct RequestsOptions {
	uint8 open = 0;
	uint8 tickets = 0;
	uint8 refusals = 4;
	bool ownerBelow = false;
};


// The number of a request, never 0. The layer returns it from Send, and the caller keeps it to identify the request.
struct RequestId {
	uint16 number = 0;
};


// The calls a ticket makes on its layer. The ticket holds a pointer to the table of its layer type, so the ticket type needs only its
// command set and its name.
struct TicketOps {
	ResultStatus (*finish)(void* layer, uint8 slotIndex, uint8 generation, uint8 status, std::span<const uint8> body);
	bool (*open)(const void* layer, uint8 slotIndex, uint8 generation);
};


// The table of a set with interim commands: the table of TicketOps and the interim call.
struct TicketInterimOps : TicketOps {
	ResultStatus (*interim)(void* layer, uint8 slotIndex, uint8 generation, uint8 status);
};


// A storage that a layer does not need: an empty type, so a member of this type costs no bytes when it has no slots.
struct NoStorage {
};


// The responder's ticket of one request: a value the handler holds and finishes, once or later. Its calls are the layer's, through its
// table. A ticket is valid while its slot holds the same generation and is open; a stale ticket answers notAvailable.
template<typename Commands, TextLiteral Name>
class Ticket {
public:
	using CommandType = CommandOf<Commands, Name>;
	using StatusType = typename CommandType::StatusType;
	using ReplyType = typename CommandType::ReplyType;


private:
	static constexpr std::size_t replySize = BodyViewOf<ReplyType>.maxSize;
	void* layer;
	const TicketOps* ops;
	uint16 number;
	uint8 index;
	uint8 generation;


public:
	// A ticket that holds no request: its calls answer notAvailable, and Open is false. A handler may store a ticket in a variable.
	Ticket() : layer(nullptr), ops(nullptr), number(0), index(0), generation(0) {
	}


	Ticket(void* layerObject, const TicketOps* operations, uint16 requestNumber, uint8 slotIndex, uint8 slotGeneration) :
		layer(layerObject), ops(operations), number(requestNumber), index(slotIndex), generation(slotGeneration) {
	}


	// An interim reply: the status, with the index of the reply counted by the layer. busy while the last interim is still unsent. The command
	// must be declared Interim(): the layer has no interim call for a set without one.
	ResultStatus Interim(StatusType status) const {
		static_assert(CommandInfoOf<Commands, Name>().interim,
			"Ticket::Interim needs the command to be declared Interim(): add .Interim() after its Reply");
		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		const TicketInterimOps* interimOps = static_cast<const TicketInterimOps*>(ops);
		return interimOps->interim(layer, index, generation, static_cast<uint8>(status));
	}


	// The final of a command with a status only. Compile error for a command with a reply body.
	ResultStatus Finish(StatusType status) const requires (std::is_same_v<ReplyType, NoBody>) {
		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		return ops->finish(layer, index, generation, static_cast<uint8>(status), std::span<const uint8>());
	}


	// The final of a command with a status and a reply body. Compile error for a command with no reply body.
	ResultStatus Finish(StatusType status, const ReplyType& body) const requires (!std::is_same_v<ReplyType, NoBody>) {
		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		std::array<uint8, replySize> bytes{};
		(void)Encode(body, std::span<uint8>(bytes.data(), bytes.size()));
		return ops->finish(layer, index, generation, static_cast<uint8>(status), std::span<const uint8>(bytes.data(), bytes.size()));
	}


	// True while the request is open: no final is queued or sent for it.
	bool Open() const {
		if (ops == nullptr) {
			return false;
		}

		return ops->open(layer, index, generation);
	}


	RequestId Number() const {
		return RequestId{number};
	}
};


// The handlers of one On entry: the check of a request body and the call of the handler with a ticket. Defined after the layer.
template<typename Commands, typename Entry>
struct OnRunner;


// The index of the On entry named name in a tuple of handler entries, or the size of the tuple when there is none.
template<typename Tuple, std::size_t... Index>
constexpr std::size_t FindOnIndexIn(std::string_view name, std::index_sequence<Index...>) {
	constexpr std::array<bool, sizeof...(Index)> isOn = {(std::tuple_element_t<Index, Tuple>::kind == HandlerKind::on)...};
	constexpr std::array<std::string_view, sizeof...(Index)> names = {std::tuple_element_t<Index, Tuple>::name...};
	for (std::size_t index = 0; index < sizeof...(Index); index++) {
		if (isOn[index] && names[index] == name) {
			return index;
		}
	}

	return sizeof...(Index);
}


template<typename HandlerSet>
constexpr std::size_t FindHandlerOn(std::string_view name) {
	using Tuple = typename HandlerSet::List;
	return FindOnIndexIn<Tuple>(name, std::make_index_sequence<std::tuple_size_v<Tuple>>());
}


// For each command of the set, the index of its On entry in the handler list, or the list's size when the command has none.
template<typename Commands, typename HandlerSet>
constexpr std::array<std::size_t, Commands::commandCount> BuildOnIndex() {
	std::array<std::size_t, Commands::commandCount> table{};
	for (std::size_t index = 0; index < Commands::commandCount; index++) {
		table[index] = FindHandlerOn<HandlerSet>(Commands::commandTable[index].name);
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
// takes no storage. The layer and its requirements read the same plan.
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
		// optional). A busy refusal is returned at once, with no number used.
		template<TextLiteral Name, auto OnFinal, auto OnInterim = nullptr>
		Result<RequestId> Send(const typename CommandOf<Commands, Name>::BodyType& body) const {
			return layer->template StartRequest<Name, OnFinal, OnInterim>(body, outbox);
		}


		// A request of a command with no reply: the packet goes without a slot and without an outcome.
		template<TextLiteral Name>
		ResultStatus Fire(const typename CommandOf<Commands, Name>::BodyType& body) const {
			return layer->template FireRequest<Name>(body, outbox);
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


	// The owner of Reliable's outcomes: a REQUEST's outcome settles its request. The other kinds of packet have no outcome here. With no
	// Reliable below there are no outcomes, and the function does nothing.
	static void DeliveryOutcome(const Outcome& outcome) {
		if constexpr (Value.ownerBelow && Value.open > 0) {
			RequestsOf* self = static_cast<RequestsOf*>(outcome.owner);
			self->OnRequestOutcome(outcome);
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
		return self->IsOpen(GetLe16(payload.data() + 2), payload[4]);
	}


	// The layer's packets are not given to Add: they go out through the handle's outbox, below the layer.
	Result<std::size_t> Add(const Meta&, std::span<const uint8>, std::span<uint8>) {
		// The layer's packets go out through its handle, which enters below the layer; a packet given to Stack::Send is not a request.
		return ResultStatus::notSupported;
	}


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
			ArmRequests(nowUs);
			ExpireRequests(nowUs);
		}

		if constexpr (Plan::served) {
			RunReceived();
			RearmTickets(nowUs);
			ExpireTickets(nowUs);
		}

		if constexpr (Plan::openCount > 0) {
			DeliverFinals();
		}

		if constexpr (Plan::served) {
			SendResponses(outbox);
		}
	}


	// The connection changed. While it is down a request of a FailWhileDown command is closed with noConnection; the others wait for it, and
	// their deadlines run. The layer has this function when a command is FailWhileDown; its state is kept only when the layer sends.
	void OnLink(bool up, uint32) requires (Commands::hasFailWhileDown) {
		if constexpr (Plan::usesLink) {
			requester.link.down = !up;
			if (!up) {
				FailDownRequests();
			}
		} else {
			(void)up;
		}
	}


	// The requester's send, from the caller's context. The request takes a free slot, its number and its tag; a refusal of the send (busy, or
	// a refusal of the stack) returns at once with no slot taken and no number used.
	template<TextLiteral Name, auto OnFinal, auto OnInterim>
	Result<RequestId> StartRequest(const typename CommandOf<Commands, Name>::BodyType& body, const Outbox& outbox) {
		using CommandType = CommandOf<Commands, Name>;
		using BodyType = typename CommandType::BodyType;
		static_assert(Plan::openCount > 0, "Send needs requester slots: give the layer Open(n) with n of at least 1");
		static_assert(CommandType::level != 1, "a Fire command has no reply: send it with Fire()");
		static_assert(CommandType::level != 2 || Value.ownerBelow,
			"a command with a delivery outcome needs Reliable below Requests; use .Fire() for no delivery at all");
		static_assert(CheckFinal<Commands, Name, OnFinal>());

		if constexpr (!std::is_same_v<decltype(OnInterim), std::nullptr_t>) {
			static_assert(CheckProgress<Commands, Name, OnInterim>());
		}

		constexpr std::size_t commandIndex = CommandIndexOf<Name>();
		constexpr CommandInfo info = Commands::commandTable[commandIndex];
		static_assert(CommandType::level == 2 || info.replyTimeoutMs > 0, "a command with a reply needs ReplyTimeoutMs: the requester's deadline");
		if constexpr (Plan::usesLink) {
			if constexpr (info.failWhileDown) {
				if (requester.link.down || !outbox.LinkUp()) {
					return ResultStatus::noConnection;
				}
			}
		}

		RequestSlot* slot = FreeRequest();
		if (slot == nullptr) {
			return ResultStatus::busy;
		}

		uint16 number = FreeNumber();
		uint8 tag = requester.numbers.nextTag;
		std::array<uint8, requestBytes> packet{};
		std::size_t length = WriteRequest<BodyType>(CommandType::id, number, tag, body, packet);
		ResultStatus status = outbox.Send(std::span<const uint8>(packet.data(), length));
		if (status != ResultStatus::ok) {
			return status;
		}

		slot->number = number;
		slot->tag = tag;
		slot->command = static_cast<uint8>(commandIndex);
		slot->result = ResultStatus::ok;
		slot->stage = RequestStage::open;
		slot->armNeeded = true;
		slot->finalFn = &RunFinal<Name, OnFinal>;
		if constexpr (Plan::anyInterim) {
			slot->interim.lastIndex = 0;
			slot->interim.interimFn = nullptr;
			if constexpr (!std::is_same_v<decltype(OnInterim), std::nullptr_t>) {
				slot->interim.interimFn = &RunInterim<Name, OnInterim>;
			}
		}

		requester.numbers.nextNumber = Successor(number);
		requester.numbers.nextTag++;
		return RequestId{number};
	}


	// The fire of a command with no reply: one packet, with the no-acknowledgement flag, so it has no slot and no outcome.
	template<TextLiteral Name>
	ResultStatus FireRequest(const typename CommandOf<Commands, Name>::BodyType& body, const Outbox& outbox) {
		using CommandType = CommandOf<Commands, Name>;
		using BodyType = typename CommandType::BodyType;
		static_assert(CommandType::level == 1, "Fire takes a command with no reply: a command with a reply is sent with Send");
		constexpr std::size_t commandIndex = CommandIndexOf<Name>();
		constexpr CommandInfo info = Commands::commandTable[commandIndex];
		if constexpr (Plan::usesLink) {
			if constexpr (info.failWhileDown) {
				if (requester.link.down || !outbox.LinkUp()) {
					return ResultStatus::noConnection;
				}
			}
		}

		uint16 number = FreeNumber();
		uint8 tag = requester.numbers.nextTag;
		std::array<uint8, requestBytes> packet{};
		std::size_t length = WriteRequest<BodyType>(CommandType::id, number, tag, body, packet);
		Meta meta{};
		meta.flags = Meta::NoAcknowledgeFlag;
		ResultStatus status = outbox.Send(std::span<const uint8>(packet.data(), length), meta);
		if (status == ResultStatus::ok) {
			requester.numbers.nextNumber = Successor(number);
			requester.numbers.nextTag++;
		}

		return status;
	}


private:
	static constexpr std::size_t handlerCount = Plan::handlerCount;
	static constexpr bool served = Plan::served;
	static constexpr bool ticketed = Plan::ticketed;
	static constexpr bool anyInterim = Plan::anyInterim;
	static constexpr std::size_t ticketCount = Plan::ticketCount;
	static constexpr std::size_t refusalCount = Plan::refusalCount;
	static constexpr std::size_t largestRequest = Commands::largestRequest;
	static constexpr std::size_t largestReply = Commands::largestReply;
	static constexpr std::size_t ticketBytes = std::max(largestRequest, RequestWire::FinalHeader + largestReply);
	static constexpr std::size_t requestBytes = RequestWire::RequestHeader + largestRequest;
	static_assert(CheckHandlers<Commands, HandlerSet>(), "the handler list does not match the command set");


	// The table of the handler index of each command (see BuildOnIndex). Its entries are the handler list's positions.
	static constexpr std::array<std::size_t, Commands::commandCount> onIndex = BuildOnIndex<Commands, HandlerSet>();


	// The stage of a requester slot: free, open (the request is waiting for its final), or ready (a final or an outcome is recorded, and the
	// handler is not yet called).
	enum class RequestStage : uint8 {
		free,
		open,
		ready
	};


	// The stage of a responder ticket: free, received (the body is copied, the handler not yet called), open (the handler has it), or final
	// (the final is encoded and waits to be sent).
	enum class TicketStage : uint8 {
		free,
		received,
		open,
		final
	};


	// The handler of a final: the result, the status and the body of the final packet. A handler that is called ready (a deadline, an
	// outcome, a link loss) gets no status and no body.
	using FinalFn = void (*)(ResultStatus, uint8, std::span<const uint8>);


	// The interim state of a requester slot: the handler and the last index. A set without interim commands has none.
	struct InterimSlot {
		void (*interimFn)(uint8) = nullptr;
		uint8 lastIndex = 0;
	};


	// One request on the requester side. The handlers are the call site's, so the slot holds the function of its Send.
	struct RequestSlot {
		FinalFn finalFn = nullptr;
		[[no_unique_address]] std::conditional_t<Plan::anyInterim, InterimSlot, NoStorage> interim{};
		uint32 deadlineUs = 0;
		uint16 number = 0;
		ResultStatus result = ResultStatus::ok;
		RequestStage stage = RequestStage::free;
		uint8 tag = 0;
		uint8 command = 0;
		bool armNeeded = false;
	};


	// The interim state of a responder ticket: the pending interim is its status and its index. The index counts the interims of the request.
	struct TicketInterim {
		uint8 status = 0;
		uint8 index = 0;
		bool pending = false;
	};


	// One request on the responder side. The bytes hold the request body, or the final packet once the final is made.
	struct TicketSlot {
		std::array<uint8, ticketBytes> bytes{};
		[[no_unique_address]] std::conditional_t<Plan::anyInterim, TicketInterim, NoStorage> interim{};
		uint32 deadlineUs = 0;
		uint16 number = 0;
		uint16 length = 0;
		TicketStage stage = TicketStage::free;
		uint8 tag = 0;
		uint8 command = 0;
		uint8 generation = 0;
		bool rearm = false;
	};


	// A refusal waiting to be sent: the final of the request with the code.
	struct Refusal {
		uint16 number = 0;
		uint8 tag = 0;
		uint8 code = 0;
	};


	struct RefusalRing {
		std::array<Refusal, Plan::refusalCount> entries{};
		std::size_t head = 0;
		std::size_t used = 0;
	};


	// The next request number and tag. A layer that sends nothing has none.
	struct NumberState {
		uint16 nextNumber = 1;
		uint8 nextTag = 1;
	};


	// The link state of a layer with a FailWhileDown command: down while the connection is not up.
	struct LinkState {
		bool down = false;
	};


	struct RequesterState {
		[[no_unique_address]] std::conditional_t<(Plan::openCount > 0), std::array<RequestSlot, Plan::openCount>, NoStorage> slots{};
		[[no_unique_address]] std::conditional_t<Plan::sends, NumberState, NoStorage> numbers{};
		[[no_unique_address]] std::conditional_t<Plan::usesLink, LinkState, NoStorage> link{};
	};


	struct ResponderState {
		std::array<TicketSlot, Plan::ticketCount> tickets{};
		[[no_unique_address]] std::conditional_t<(Plan::refusalCount > 0), RefusalRing, NoStorage> refusals{};
	};


	[[no_unique_address]] RequesterState requester{};
	[[no_unique_address]] std::conditional_t<served, ResponderState, NoStorage> responder{};


public:
	static const TicketOps* Operations() {
		if constexpr (anyInterim) {
			static constexpr TicketInterimOps operations = {{&FinishOp, &OpenOp}, &InterimOp};
			return &operations;
		} else {
			static constexpr TicketOps operations = {&FinishOp, &OpenOp};
			return &operations;
		}
	}


private:
	// The command index of the command named Name, in the table of the set.
	template<TextLiteral Name>
	static constexpr std::size_t CommandIndexOf() {
		for (std::size_t index = 0; index < Commands::commandCount; index++) {
			if (Commands::commandTable[index].name == Name.View()) {
				return index;
			}
		}

		return Commands::commandCount;
	}


	static constexpr uint16 Successor(uint16 number) {
		return number == 0xFFFF ? static_cast<uint16>(1) : static_cast<uint16>(number + 1);
	}


	// Writes a REQUEST packet: the kind, the command id, the number, the tag and the body. Returns the length.
	template<typename BodyType>
	static std::size_t WriteRequest(uint8 id, uint16 number, uint8 tag, const BodyType& body, std::array<uint8, requestBytes>& packet) {
		packet[0] = RequestWire::KindRequest;
		packet[1] = id;
		PutLe16(packet.data() + 2, number);
		packet[4] = tag;
		std::size_t size = BodyViewOf<BodyType>.maxSize;
		(void)Encode(body, std::span<uint8>(packet.data() + RequestWire::RequestHeader, size));
		return RequestWire::RequestHeader + size;
	}


	// A number that no open request has, and that is not 0. The counter moves past it only when the request is sent.
	uint16 FreeNumber() const {
		uint16 candidate = requester.numbers.nextNumber;
		while (candidate == 0 || NumberOpen(candidate)) {
			candidate = Successor(candidate);
		}

		return candidate;
	}


	bool NumberOpen(uint16 number) const {
		if constexpr (Plan::openCount > 0) {
			for (const RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open && slot.number == number) {
					return true;
				}
			}
		} else {
			(void)number;
		}

		return false;
	}


	RequestSlot* FreeRequest() {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::free) {
					return &slot;
				}
			}
		}

		return nullptr;
	}


	// The open request of a number and tag: only an open one (not yet closed) is returned.
	RequestSlot* FindOpenRequest(uint16 number, uint8 tag) {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open && slot.number == number && slot.tag == tag) {
					return &slot;
				}
			}
		} else {
			(void)number;
			(void)tag;
		}

		return nullptr;
	}


	// True while the request of a number and tag is open on the requester side. Reliable asks it through DeliveryWanted.
	bool IsOpen(uint16 number, uint8 tag) const {
		if constexpr (Plan::openCount > 0) {
			for (const RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open && slot.number == number && slot.tag == tag) {
					return true;
				}
			}
		} else {
			(void)number;
			(void)tag;
		}

		return false;
	}


	// Ends a request that is waiting for its handler: the first result wins, and the handler is called once, in Process.
	static void Settle(RequestSlot& slot, ResultStatus status) {
		if (slot.stage == RequestStage::open) {
			slot.stage = RequestStage::ready;
			slot.result = status;
		}
	}


	// Ends a request with its final, from Take: the slot is freed first, then the handler is called with the body of the packet.
	static void CloseRequest(RequestSlot& slot, ResultStatus result, uint8 status, std::span<const uint8> body) {
		FinalFn handler = slot.finalFn;
		slot.stage = RequestStage::free;
		handler(result, status, body);
	}


	void ArmRequests(uint32 nowUs) {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open && slot.armNeeded) {
					slot.armNeeded = false;
					slot.deadlineUs = nowUs + TimeoutUsOf(slot.command);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	uint32 TimeoutUsOf(uint8 command) const {
		return Commands::commandTable[command].replyTimeoutMs * 1000u;
	}


	void ExpireRequests(uint32 nowUs) {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				bool timed = Commands::commandTable[slot.command].replyTimeoutMs > 0;
				if (slot.stage == RequestStage::open && timed && !slot.armNeeded && RequestWire::Due(nowUs, slot.deadlineUs)) {
					Settle(slot, ResultStatus::timeout);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	// The handlers of the ready requests. Each slot is freed, and then its handler runs.
	void DeliverFinals() {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::ready) {
					FinalFn handler = slot.finalFn;
					ResultStatus result = slot.result;
					slot.stage = RequestStage::free;
					handler(result, 0, std::span<const uint8>());
				}
			}
		}
	}


	void FailDownRequests() {
		if constexpr (Plan::usesLink) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open && Commands::commandTable[slot.command].failWhileDown) {
					Settle(slot, ResultStatus::noConnection);
				}
			}
		}
	}


	// The outcome of a REQUEST: a failure settles its request, and a command with no reply settles it with ok. A command with a reply waits for
	// its final, and the outcome ok is only the delivery.
	void OnRequestOutcome(const Outcome& outcome) {
		if constexpr (Plan::openCount > 0) {
			std::span<const uint8> payload = outcome.payload;
			if (payload.size() < RequestWire::RequestHeader || payload[0] != RequestWire::KindRequest) {
				return;
			}

			RequestSlot* slot = FindOpenRequest(GetLe16(payload.data() + 2), payload[4]);
			if (slot == nullptr) {
				return;
			}

			if (outcome.status != ResultStatus::ok || Commands::commandTable[slot->command].level == 2) {
				Settle(*slot, outcome.status);
			}
		} else {
			(void)outcome;
		}
	}


	// Takes a FINAL of one of this layer's requests. A FINAL closes its request, and its handler is called here, with the body the packet has.
	// A bad body or status closes it as invalidData; a late or duplicate final is dropped.
	Result<Taken> TakeFinal(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::FinalErrorSize) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::openCount > 0) {
			RequestSlot* slot = FindOpenRequest(GetLe16(packet.data() + 1), packet[3]);
			if (slot != nullptr) {
				uint8 code = packet[4];
				ResultStatus result = ResultStatus::ok;
				uint8 status = 0;
				std::span<const uint8> body;
				if (code != RequestWire::CodeOk) {
					result = RequestWire::ResultOfCode(code);
				} else if (packet.size() < RequestWire::FinalHeader || packet[5] >= RequestWire::StatusReservedFrom) {
					result = ResultStatus::invalidData;
				} else {
					status = packet[5];
					body = packet.subspan(RequestWire::FinalHeader);
				}

				CloseRequest(*slot, result, status, body);
			}
		}

		return Taken{0, false};
	}


	// Takes an INTERIM of an open request: the index must rise, and the status must be a user status. The handler is called here, at once,
	// so no reply waits in a queue. The deadline of the request is re-armed in Process.
	Result<Taken> TakeInterim(std::span<const uint8> packet) {
		if (packet.size() < RequestWire::InterimSize) {
			return ResultStatus::invalidData;
		}

		if constexpr (Plan::openCount > 0 && anyInterim) {
			RequestSlot* slot = FindOpenRequest(GetLe16(packet.data() + 1), packet[3]);
			uint8 index = packet[4];
			uint8 status = packet[5];
			if (slot == nullptr) {
				return Taken{0, false};
			}

			if (status >= RequestWire::StatusReservedFrom) {
				return ResultStatus::invalidData;
			}

			if (index == 0 || index <= slot->interim.lastIndex) {
				return Taken{0, false};
			}

			slot->interim.lastIndex = index;
			slot->armNeeded = true;
			void (*handler)(uint8) = slot->interim.interimFn;
			if (handler != nullptr) {
				handler(status);
			}
		}

		return Taken{0, false};
	}


	// Takes a REQUEST, the responder's side. A request with a reply takes a ticket, or is refused from the ring; a command with no reply runs at
	// once, after the requests already received in this walk. The packet is consumed in every case that is not an error.
	Result<Taken> TakeRequest(std::span<const uint8> packet) {
		if constexpr (!served) {
			(void)packet;
			return ResultStatus::notSupported;
		} else {
			static_assert(!ticketed || Value.tickets > 0, "the layer serves a command and has no tickets: give Tickets(n) with n of at least 1");
			if (packet.size() < RequestWire::RequestHeader) {
				return ResultStatus::invalidData;
			}

			uint8 id = packet[1];
			uint16 number = GetLe16(packet.data() + 2);
			uint8 tag = packet[4];
			if (number == 0) {
				return ResultStatus::invalidData;
			}

			std::size_t command = CommandIndexById(id);
			bool answers = command >= Commands::commandCount || Commands::commandTable[command].level >= 3;
			if constexpr (ticketed) {
				TicketSlot* open = FindTicketByNumber(number);
				if (open != nullptr && answers) {
					if (open->tag != tag) {
						Refuse(number, tag, RequestWire::CodeBusy);
					}

					return Taken{0, false};
				}
			}

			uint8 code = AdmitByIndex(command, number, tag, packet.subspan(RequestWire::RequestHeader));
			if (code == RequestWire::CodeOk) {
				return Taken{0, false};
			}

			if (answers) {
				Refuse(number, tag, code);
				return Taken{0, false};
			}

			if (code == RequestWire::CodeNotSupported) {
				return ResultStatus::notSupported;
			}

			return ResultStatus::invalidData;
		}
	}


	std::size_t CommandIndexById(uint8 id) const {
		for (std::size_t index = 0; index < Commands::commandCount; index++) {
			if (Commands::commandTable[index].id == id) {
				return index;
			}
		}

		return Commands::commandCount;
	}


	// The refusal code of a request that is taken: 0 when it was admitted, 2, 3 or 4 when it was refused. The index of an unknown command
	// is commandCount, and that is refused as notSupported.
	uint8 AdmitByIndex(std::size_t command, uint16 number, uint8 tag, std::span<const uint8> body) {
		return AdmitAt(command, number, tag, body, std::make_index_sequence<Commands::commandCount>());
	}


	template<std::size_t... Index>
	uint8 AdmitAt(std::size_t command, uint16 number, uint8 tag, std::span<const uint8> body, std::index_sequence<Index...>) {
		uint8 code = RequestWire::CodeNotSupported;
		(void)command;
		(void)number;
		(void)tag;
		(void)body;
		((command == Index ? (code = AdmitOne<Index>(number, tag, body), true) : false) || ...);
		return code;
	}


	template<std::size_t Index>
	uint8 AdmitOne(uint16 number, uint8 tag, std::span<const uint8> body) {
		constexpr std::size_t handler = onIndex[Index];
		if constexpr (handler >= handlerCount) {
			(void)number;
			(void)tag;
			(void)body;
			return RequestWire::CodeNotSupported;
		} else {
			using Entry = std::tuple_element_t<(handler < handlerCount ? handler : 0), typename HandlerSet::List>;
			using Runner = OnRunner<Commands, Entry>;
			using BodyType = typename Runner::BodyType;
			if (Runner::Check(body) != ResultStatus::ok) {
				return RequestWire::CodeInvalidData;
			}

			if constexpr (Runner::hasTicket) {
				TicketSlot* slot = FreeTicket();
				if (slot == nullptr) {
					return RequestWire::CodeBusy;
				}

				std::size_t length = std::min(body.size(), BodyViewOf<BodyType>.maxSize);
				for (std::size_t byte = 0; byte < length; byte++) {
					slot->bytes[byte] = body[byte];
				}

				slot->number = number;
				slot->tag = tag;
				slot->command = static_cast<uint8>(Index);
				slot->length = static_cast<uint16>(length);
				slot->generation++;
				if constexpr (anyInterim) {
					slot->interim = TicketInterim{};
				}

				slot->rearm = false;
				slot->stage = TicketStage::received;
				return RequestWire::CodeOk;
			} else {
				RunReceived();
				Runner::Call(nullptr, nullptr, number, 0, 0, body);
				return RequestWire::CodeOk;
			}
		}
	}


	TicketSlot* FreeTicket() {
		if constexpr (ticketCount > 0) {
			for (TicketSlot& slot : responder.tickets) {
				if (slot.stage == TicketStage::free) {
					return &slot;
				}
			}
		}

		return nullptr;
	}


	TicketSlot* FindTicketByNumber(uint16 number) {
		if constexpr (ticketCount > 0) {
			for (TicketSlot& slot : responder.tickets) {
				if (slot.stage != TicketStage::free && slot.number == number) {
					return &slot;
				}
			}
		} else {
			(void)number;
		}

		return nullptr;
	}


	void Refuse(uint16 number, uint8 tag, uint8 code) {
		if constexpr (refusalCount > 0) {
			if (responder.refusals.used == refusalCount) {
				return;
			}

			std::size_t position = (responder.refusals.head + responder.refusals.used) % refusalCount;
			responder.refusals.entries[position] = Refusal{number, tag, code};
			responder.refusals.used++;
		} else {
			(void)number;
			(void)tag;
			(void)code;
		}
	}


	// Runs the handlers of the received requests, in arrival order. A received request's ticket is open after this: its deadline is armed in
	// Process. It takes no clock, so Take can call it: a command with no reply runs after the requests received earlier in the walk.
	void RunReceived() {
		if constexpr (ticketCount > 0) {
			for (std::size_t index = 0; index < ticketCount; index++) {
				TicketSlot& slot = responder.tickets[index];
				if (slot.stage != TicketStage::received) {
					continue;
				}

				slot.stage = TicketStage::open;
				slot.rearm = true;
				CallCommand(slot.command, static_cast<uint8>(index), slot, std::make_index_sequence<Commands::commandCount>());
			}
		}
	}


	template<std::size_t... Index>
	void CallCommand(uint8 command, uint8 slotIndex, TicketSlot& slot, std::index_sequence<Index...>) {
		(void)command;
		(void)slotIndex;
		(void)slot;
		((command == Index ? (CallOne<Index>(slotIndex, slot), true) : false) || ...);
	}


	template<std::size_t Index>
	void CallOne(uint8 slotIndex, TicketSlot& slot) {
		constexpr std::size_t handler = onIndex[Index];
		if constexpr (handler < handlerCount) {
			using Entry = std::tuple_element_t<handler, typename HandlerSet::List>;
			OnRunner<Commands, Entry>::Call(
				this,
				Operations(),
				slot.number,
				slotIndex,
				slot.generation,
				std::span<const uint8>(slot.bytes.data(), slot.length)
			);
		} else {
			(void)slotIndex;
			(void)slot;
		}
	}


	// Arms the deadline of each ticket that was opened or given an interim since the last Process.
	void RearmTickets(uint32 nowUs) {
		if constexpr (ticketCount > 0) {
			for (TicketSlot& slot : responder.tickets) {
				if (slot.stage == TicketStage::open && slot.rearm) {
					slot.rearm = false;
					slot.deadlineUs = nowUs + Commands::commandTable[slot.command].ticketTimeoutMs * 1000u;
				}
			}
		} else {
			(void)nowUs;
		}
	}


	// A ticket that its handler never finished ends with the final timeout (code 5).
	void ExpireTickets(uint32 nowUs) {
		if constexpr (ticketCount > 0) {
			for (TicketSlot& slot : responder.tickets) {
				if (slot.stage == TicketStage::open && RequestWire::Due(nowUs, slot.deadlineUs)) {
					WriteFinalError(slot, RequestWire::CodeTimeout);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	static void WriteFinalError(TicketSlot& slot, uint8 code) {
		slot.bytes[0] = RequestWire::KindFinal;
		PutLe16(slot.bytes.data() + 1, slot.number);
		slot.bytes[3] = slot.tag;
		slot.bytes[4] = code;
		slot.length = RequestWire::FinalErrorSize;
		slot.stage = TicketStage::final;
	}


	// Sends what is waiting, in order: the refusals, then each ticket's interim and final. A busy send keeps its packet for the next Process;
	// any other result ends it.
	void SendResponses(const Outbox& outbox) {
		if constexpr (refusalCount > 0) {
			while (responder.refusals.used > 0) {
				Refusal& refusal = responder.refusals.entries[responder.refusals.head];
				std::array<uint8, RequestWire::FinalErrorSize> packet{};
				packet[0] = RequestWire::KindFinal;
				PutLe16(packet.data() + 1, refusal.number);
				packet[3] = refusal.tag;
				packet[4] = refusal.code;
				if (outbox.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::busy) {
					return;
				}

				responder.refusals.head = (responder.refusals.head + 1) % refusalCount;
				responder.refusals.used--;
			}
		}

		if constexpr (served) {
			for (TicketSlot& slot : responder.tickets) {
				if constexpr (anyInterim) {
					if (slot.interim.pending) {
						std::array<uint8, RequestWire::InterimSize> packet{};
						packet[0] = RequestWire::KindInterim;
						PutLe16(packet.data() + 1, slot.number);
						packet[3] = slot.tag;
						packet[4] = slot.interim.index;
						packet[5] = slot.interim.status;
						if (outbox.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::busy) {
							continue;
						}

						slot.interim.pending = false;
					}
				}

				if (slot.stage == TicketStage::final) {
					ResultStatus status = outbox.Send(std::span<const uint8>(slot.bytes.data(), slot.length));
					if (status != ResultStatus::busy) {
						slot.stage = TicketStage::free;
					}
				}
			}
		}
	}


	// The live ticket of a handle's call: its slot holds the generation and is open.
	bool TicketLive(uint8 index, uint8 generation) const {
		if constexpr (ticketCount > 0) {
			if (index < ticketCount) {
				const TicketSlot& slot = responder.tickets[index];
				return slot.stage == TicketStage::open && slot.generation == generation;
			}
		}

		return false;
	}


	ResultStatus QueueInterim(uint8 index, uint8 generation, uint8 status) {
		if constexpr (anyInterim && served) {
			if (!TicketLive(index, generation)) {
				return ResultStatus::notAvailable;
			}

			TicketSlot& slot = responder.tickets[index];
			if (slot.interim.pending) {
				return ResultStatus::busy;
			}

			if (slot.interim.index == 255) {
				return ResultStatus::invalidParameter;
			}

			slot.interim.index++;
			slot.interim.status = status;
			slot.interim.pending = true;
			slot.rearm = true;
			return ResultStatus::ok;
		} else {
			(void)index;
			(void)generation;
			(void)status;
			return ResultStatus::notAvailable;
		}
	}


	ResultStatus QueueFinal(uint8 index, uint8 generation, uint8 status, std::span<const uint8> body) {
		if constexpr (served) {
			if (!TicketLive(index, generation)) {
				return ResultStatus::notAvailable;
			}

			TicketSlot& slot = responder.tickets[index];
			slot.bytes[0] = RequestWire::KindFinal;
			PutLe16(slot.bytes.data() + 1, slot.number);
			slot.bytes[3] = slot.tag;
			slot.bytes[4] = RequestWire::CodeOk;
			slot.bytes[5] = status;
			std::size_t length = std::min(body.size(), ticketBytes - RequestWire::FinalHeader);
			for (std::size_t byte = 0; byte < length; byte++) {
				slot.bytes[RequestWire::FinalHeader + byte] = body[byte];
			}

			slot.length = static_cast<uint16>(RequestWire::FinalHeader + length);
			slot.stage = TicketStage::final;
			return ResultStatus::ok;
		} else {
			(void)index;
			(void)generation;
			(void)status;
			(void)body;
			return ResultStatus::notAvailable;
		}
	}


	static ResultStatus InterimOp(void* layer, uint8 index, uint8 generation, uint8 status) {
		RequestsOf* self = static_cast<RequestsOf*>(layer);
		return self->QueueInterim(index, generation, status);
	}


	static ResultStatus FinishOp(void* layer, uint8 index, uint8 generation, uint8 status, std::span<const uint8> body) {
		RequestsOf* self = static_cast<RequestsOf*>(layer);
		return self->QueueFinal(index, generation, status, body);
	}


	static bool OpenOp(const void* layer, uint8 index, uint8 generation) {
		const RequestsOf* self = static_cast<const RequestsOf*>(layer);
		return self->TicketLive(index, generation);
	}


	// The handler of a final: the result and, for a reply, the status and the body, decoded from the bytes of the packet.
	template<TextLiteral Name, auto OnFinal>
	static void RunFinal(ResultStatus result, uint8 status, std::span<const uint8> body) {
		using CommandType = CommandOf<Commands, Name>;
		if constexpr (CommandType::level == 2) {
			(void)status;
			(void)body;
			OnFinal(result);
		} else {
			using FinalType = Final<typename CommandType::StatusType, typename CommandType::ReplyType>;
			FinalType reply{};
			reply.result = result;
			reply.status = static_cast<typename CommandType::StatusType>(status);
			if (result == ResultStatus::ok) {
				ResultStatus decoded = Decode(body, reply.body);
				if (decoded != ResultStatus::ok) {
					reply.result = decoded;
				}
			}

			OnFinal(reply);
		}
	}


	template<TextLiteral Name, auto OnInterim>
	static void RunInterim(uint8 status) {
		using CommandType = CommandOf<Commands, Name>;
		Progress<typename CommandType::StatusType> progress{};
		progress.status = static_cast<typename CommandType::StatusType>(status);
		OnInterim(progress);
	}


	// The restart: the open requests close with reset, and the responder's tickets and refusals are dropped.
	void OnPeerRestart() {
		if constexpr (Plan::openCount > 0) {
			for (RequestSlot& slot : requester.slots) {
				if (slot.stage == RequestStage::open) {
					Settle(slot, ResultStatus::reset);
				}
			}
		}

		if constexpr (served) {
			for (TicketSlot& slot : responder.tickets) {
				slot.stage = TicketStage::free;
				if constexpr (anyInterim) {
					slot.interim.pending = false;
				}
			}

			if constexpr (refusalCount > 0) {
				responder.refusals.used = 0;
				responder.refusals.head = 0;
			}
		}
	}
};


template<typename Commands, typename Entry>
struct OnRunner;


template<typename Commands, TextLiteral Name, auto Fn>
struct OnRunner<Commands, On<Name, Fn>> {
	using CommandType = CommandOf<Commands, Name>;
	using BodyType = typename CommandType::BodyType;
	using TicketType = Ticket<Commands, Name>;
	static constexpr bool hasTicket = CommandType::level >= 3;


	// The check of a request body: it decodes, so a bad body is refused and no ticket is taken for it.
	static ResultStatus Check(std::span<const uint8> body) {
		BodyType scratch{};
		return Decode(body, scratch);
	}


	// The call of the handler. A command with a reply gets its ticket; the others get the body only.
	static void Call(
		void* layer,
		const TicketOps* operations,
		uint16 number,
		uint8 slotIndex,
		uint8 generation,
		std::span<const uint8> body)
	{
		if constexpr (std::is_same_v<BodyType, NoBody>) {
			if constexpr (hasTicket) {
				Fn(TicketType(layer, operations, number, slotIndex, generation));
			} else {
				(void)layer;
				(void)operations;
				(void)number;
				(void)slotIndex;
				(void)generation;
				(void)body;
				Fn();
			}
		} else {
			BodyType value{};
			(void)Decode(body, value);
			if constexpr (hasTicket) {
				Fn(TicketType(layer, operations, number, slotIndex, generation), value);
			} else {
				(void)layer;
				(void)operations;
				(void)number;
				(void)slotIndex;
				(void)generation;
				Fn(value);
			}
		}
	}
};


// The requests layer with its command set and handler list. Requests is the layer with default options.
template<typename Commands, typename HandlerSet>
using Requests = RequestsOf<Commands, HandlerSet>;


}
