#pragma once
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
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
// command set and its name.
struct TicketOps {
	ResultStatus (*finish)(void* layer, uint8 slotIndex, uint8 generation, uint8 status, std::span<const uint8> body);
	bool (*open)(const void* layer, uint8 slotIndex, uint8 generation);
};


// The table of a set with interim commands: the table of TicketOps and the interim call.
struct TicketInterimOps : TicketOps {
	ResultStatus (*interim)(void* layer, uint8 slotIndex, uint8 generation, uint8 status);
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

}
