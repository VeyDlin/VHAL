#pragma once
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


namespace VUB {


// The storage of a part that is off: an empty type, so a member of this type costs no bytes when its part has no slots or no tickets.
struct NoStorage {
};


// The number of a request, never 0. The layer returns it from Send, and the caller keeps it to identify the request.
struct RequestId {
	uint16 number = 0;
};


// The calls a ticket makes on its layer. The ticket holds a pointer to the table of its layer type, so the ticket type needs only its
// command set and its id. A call names the slot, its generation and the request number: a ticket is live only when all three match.
struct TicketOps {
	ResultStatus (*finish)(
		void* layer,
		uint8 slotIndex,
		uint8 generation,
		uint16 number,
		uint8 status,
		std::span<const uint8> body);
	bool (*open)(const void* layer, uint8 slotIndex, uint8 generation, uint16 number);
};


// The table of a set with interim commands: the table of TicketOps and the interim call.
struct TicketInterimOps : TicketOps {
	ResultStatus (*interim)(
		void* layer,
		uint8 slotIndex,
		uint8 generation,
		uint16 number,
		uint8 status);
};


// The table of a set with a cancellable command: the cancelled call.
struct TicketCancelOps : TicketOps {
	ResultStatus (*cancelled)(void* layer, uint8 slotIndex, uint8 generation, uint16 number);
};


// The table of a set with interim commands and a cancellable command: both calls.
struct TicketCancelInterimOps : TicketInterimOps {
	ResultStatus (*cancelled)(void* layer, uint8 slotIndex, uint8 generation, uint16 number);
};


// True when the command set has a cancellable command. A set without one has no cancel call.
template<typename Commands>
inline constexpr bool SetIsCancellable = AnyCommandFlag(Commands::commandTable, &CommandInfo::cancellable);


// The table type of a set: it holds exactly the calls the set has, so a call a set does not have is not built.
template<typename Commands>
using TicketOpsOf = std::conditional_t<
	Commands::hasInterim && SetIsCancellable<Commands>,
	TicketCancelInterimOps,
	std::conditional_t<
		Commands::hasInterim,
		TicketInterimOps,
		std::conditional_t<SetIsCancellable<Commands>, TicketCancelOps, TicketOps>>>;


// The responder's ticket of one request: a value the handler holds and finishes at any time. Its calls are the layer's, through its
// table. A ticket is valid while its slot holds the same generation and the same request number, and is open; any other ticket answers
// notAvailable.
template<typename Commands, auto Id>
class Ticket {
public:
	using CommandType = CommandOf<Commands, Id>;
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


	Ticket(
		void* layerObject,
		const TicketOps* operations,
		uint16 requestNumber,
		uint8 slotIndex,
		uint8 slotGeneration) :
		layer(layerObject), ops(operations), number(requestNumber), index(slotIndex), generation(slotGeneration)
	{
	}


	// An interim reply: the status, with the index of the reply counted by the layer. busy while the last interim is still unsent. The command
	// must be declared Interim(): the layer has no interim call for a set without one. A status of 0xF0 or more is invalidParameter, and
	// nothing is sent.
	ResultStatus Interim(StatusType status) const {
		static_assert(CommandInfoOf<Commands, Id>().interim,
			"Ticket::Interim needs the command to be declared Interim(): add .Interim() after its Reply");
		if (static_cast<uint8>(status) >= RequestWire::StatusReservedFrom) {
			return ResultStatus::invalidParameter;
		}

		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		const TicketOpsOf<Commands>* setOps = static_cast<const TicketOpsOf<Commands>*>(ops);
		return setOps->interim(layer, index, generation, number, static_cast<uint8>(status));
	}


	// The final of a command with a status only. Compile error for a command with a reply body. A status of 0xF0 or more is invalidParameter,
	// and nothing is sent.
	ResultStatus Finish(StatusType status) const requires (std::is_same_v<ReplyType, NoBody>) {
		if (static_cast<uint8>(status) >= RequestWire::StatusReservedFrom) {
			return ResultStatus::invalidParameter;
		}

		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		return ops->finish(layer, index, generation, number, static_cast<uint8>(status), std::span<const uint8>());
	}


	// The final of a command with a status and a reply body. Compile error for a command with no reply body. A status of 0xF0 or more is
	// invalidParameter, and nothing is sent.
	ResultStatus Finish(StatusType status, const ReplyType& body) const requires (!std::is_same_v<ReplyType, NoBody>) {
		if (static_cast<uint8>(status) >= RequestWire::StatusReservedFrom) {
			return ResultStatus::invalidParameter;
		}

		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		std::array<uint8, replySize> bytes{};
		(void)Encode(body, std::span<uint8>(bytes.data(), bytes.size()));
		return ops->finish(layer, index, generation, number, static_cast<uint8>(status), std::span<const uint8>(bytes.data(), bytes.size()));
	}


	// The final operationCancelled. It is queued while the ticket is open, from any handler of the request. Only a set with a cancellable
	// command has the call.
	ResultStatus Cancelled() const requires (SetIsCancellable<Commands>) {
		if (ops == nullptr) {
			return ResultStatus::notAvailable;
		}

		const TicketOpsOf<Commands>* setOps = static_cast<const TicketOpsOf<Commands>*>(ops);
		return setOps->cancelled(layer, index, generation, number);
	}


	// True while the request is open: no final is queued or sent for it.
	bool Open() const {
		if (ops == nullptr) {
			return false;
		}

		return ops->open(layer, index, generation, number);
	}


	RequestId Number() const {
		return RequestId{number};
	}
};

}
