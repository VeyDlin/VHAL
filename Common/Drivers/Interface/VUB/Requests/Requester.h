#pragma once
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests/Ticket.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


namespace VUB {


// The stage of a requester slot: free, open (the request is waiting for its final), or ready (a final or an outcome is recorded, and the
// handler is still to be called).
enum class RequestStage : uint8 {
	free,
	open,
	ready
};


// The handler of a final: the result, the status and the body of the final packet. A handler that is called ready (a deadline, an
// outcome, a link loss) gets no status and no body.
using RequestFinalFn = void (*)(ResultStatus, uint8, std::span<const uint8>);


// The handler of an interim: the status of the interim reply.
using RequestInterimFn = void (*)(uint8);


// The interim state of a requester slot: the handler and the last index. A set without interim commands has none.
struct RequestInterimSlot {
	RequestInterimFn interimFn = nullptr;
	uint8 lastIndex = 0;
};


// One request on the requester side. The handlers are the call site's, so the slot holds the function of its Send. The interim part is a
// member only when the set has interim commands. The slot's own calls take no command type: a Send site adds only its encoding and its
// final handler.
template<bool Interim>
struct RequestSlot {
	RequestFinalFn finalFn = nullptr;
	[[no_unique_address]] std::conditional_t<Interim, RequestInterimSlot, NoStorage> interim{};
	uint32 deadlineUs = 0;
	uint16 number = 0;
	ResultStatus result = ResultStatus::ok;
	RequestStage stage = RequestStage::free;
	uint8 tag = 0;
	uint8 command = 0;
	bool armNeeded = false;


	// Ends a request that is waiting for its handler: the first result wins, and the handler is called once, in Process.
	void Settle(ResultStatus status) {
		if (stage == RequestStage::open) {
			stage = RequestStage::ready;
			result = status;
		}
	}


	// Ends a request with its final, from Take: the slot is freed first, then the handler is called with the body of the packet.
	void Close(ResultStatus finalResult, uint8 status, std::span<const uint8> body) {
		RequestFinalFn handler = finalFn;
		stage = RequestStage::free;
		handler(finalResult, status, body);
	}
};


// The next request number and tag. A layer that sends nothing has none.
struct RequestNumbers {
	uint16 nextNumber = 1;
	uint8 nextTag = 1;
};


// The session of a layer with no Reliable below: a sending layer is not ready until its session value is given.
struct RequestSession {
	bool given = false;
};


// The link state of a layer with a FailWhileDown command: down while the connection is not up.
struct RequestLink {
	bool down = false;
};


// The requester side of the requests layer: the open requests, their numbers and tags, and the link state of a FailWhileDown command. The
// layer gives its plan (the counts and flags it was built with), so a part that is off has no member of its own and takes no storage.
template<typename Commands, typename Plan>
class RequesterOf {
private:
	using Slot = RequestSlot<Plan::anyInterim>;
	[[no_unique_address]] std::conditional_t<(Plan::openCount > 0), std::array<Slot, Plan::openCount>, NoStorage> slots{};
	[[no_unique_address]] std::conditional_t<Plan::sends, RequestNumbers, NoStorage> numbers{};
	[[no_unique_address]] std::conditional_t<Plan::usesLink, RequestLink, NoStorage> link{};
	[[no_unique_address]] std::conditional_t<(Plan::sends && !Plan::ownerBelow), RequestSession, NoStorage> session{};
	static constexpr std::size_t requestBytes = RequestWire::RequestHeader + Commands::largestRequest;
	static constexpr uint32 SessionStep = 0x9E37;


public:
	// Sets the first number and the first tag of the session, for a layer with no Reliable below. Each boot gives a different value. The first
	// number is the value times SessionStep, an odd factor: different values give different first numbers, and the first numbers of values
	// that follow each other are far apart. The first tag is the low byte of the value. The number and the tag advance together, so the
	// requests of two boots whose values follow each other never have the same number and tag. Number 0 is never sent, so a value of 0
	// starts at number 1.
	void SetSession(uint16 value) {
		if constexpr (Plan::sends && !Plan::ownerBelow) {
			numbers.nextNumber = static_cast<uint16>(static_cast<uint32>(value) * SessionStep);
			numbers.nextTag = static_cast<uint8>(value & 0xFF);
			session.given = true;
		} else {
			(void)value;
		}
	}


	// The requester's send, from the caller's context. The body is encoded here; the rest of the request is Launch, which takes no command
	// types. A refusal of the send (busy, or a refusal of the stack) returns at once with no slot taken and no number used. With no Reliable
	// below, a send while no session is given returns noInit.
	template<auto Id, auto OnFinal, auto OnInterim>
	Result<RequestId> Start(const typename CommandOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		using CommandType = CommandOf<Commands, Id>;
		using BodyType = typename CommandType::BodyType;
		static_assert(Plan::openCount > 0, "Send needs requester slots: give the layer Open(n) with n of at least 1");
		static_assert(CommandType::level != 1, "a Fire command has no reply: send it with Fire()");
		static_assert(CommandType::level != 2 || Plan::ownerBelow,
			"a command with a delivery outcome needs Reliable below Requests; use .Fire() for no delivery at all");
		static_assert(CheckFinal<Commands, Id, OnFinal>());

		if constexpr (!std::is_same_v<decltype(OnInterim), std::nullptr_t>) {
			static_assert(CheckProgress<Commands, Id, OnInterim>());
		}

		if constexpr (!Plan::ownerBelow) {
			if (!session.given) {
				return ResultStatus::noInit;
			}
		}

		constexpr std::size_t commandIndex = CommandIndexOf<Id>();
		constexpr CommandInfo info = Commands::commandTable[commandIndex];
		static_assert(CommandType::level == 2 || info.replyTimeoutMs > 0, "a command with a reply needs ReplyTimeoutMs: the requester's deadline");
		if constexpr (Plan::usesLink) {
			if constexpr (info.failWhileDown) {
				if (link.down || !outbox.LinkUp()) {
					return ResultStatus::noConnection;
				}
			}
		}

		std::array<uint8, requestBytes> packet{};
		constexpr std::size_t bodyLength = BodyViewOf<BodyType>.maxSize;
		(void)Encode(body, std::span<uint8>(packet.data() + RequestWire::RequestHeader, bodyLength));
		RequestFinalFn finalFn = &RunFinal<Id, OnFinal>;
		RequestInterimFn interimFn = nullptr;
		if constexpr (Plan::anyInterim) {
			if constexpr (!std::is_same_v<decltype(OnInterim), std::nullptr_t>) {
				interimFn = &RunInterim<Id, OnInterim>;
			}
		}

		return Launch(commandIndex, CommandType::id, packet, bodyLength, finalFn, interimFn, outbox);
	}


	// The fire of a command with no reply: one packet, with the no-acknowledgement flag, so it has no slot and no outcome. It needs no session.
	template<auto Id>
	ResultStatus Fire(const typename CommandOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		using CommandType = CommandOf<Commands, Id>;
		using BodyType = typename CommandType::BodyType;
		static_assert(CommandType::level == 1, "Fire takes a command with no reply: a command with a reply is sent with Send");
		constexpr std::size_t commandIndex = CommandIndexOf<Id>();
		constexpr CommandInfo info = Commands::commandTable[commandIndex];
		if constexpr (Plan::usesLink) {
			if constexpr (info.failWhileDown) {
				if (link.down || !outbox.LinkUp()) {
					return ResultStatus::noConnection;
				}
			}
		}

		std::array<uint8, requestBytes> packet{};
		constexpr std::size_t bodyLength = BodyViewOf<BodyType>.maxSize;
		(void)Encode(body, std::span<uint8>(packet.data() + RequestWire::RequestHeader, bodyLength));
		return LaunchFire(CommandType::id, packet, bodyLength, outbox);
	}


	// Arms the deadline of each request whose packet was sent since the last Process.
	void Arm(uint32 nowUs) {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
				if (slot.stage == RequestStage::open && slot.armNeeded) {
					slot.armNeeded = false;
					slot.deadlineUs = nowUs + TimeoutUsOf(slot.command);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	// Settles each open request whose deadline has passed with timeout. A command with no ReplyTimeoutMs has no deadline.
	void Expire(uint32 nowUs) {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
				bool timed = Commands::commandTable[slot.command].replyTimeoutMs > 0;
				if (slot.stage == RequestStage::open && timed && !slot.armNeeded && RequestWire::Due(nowUs, slot.deadlineUs)) {
					slot.Settle(ResultStatus::timeout);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	// The handlers of the ready requests. Each slot is freed, and then its handler runs.
	void Deliver() {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
				if (slot.stage == RequestStage::ready) {
					RequestFinalFn handler = slot.finalFn;
					ResultStatus result = slot.result;
					slot.stage = RequestStage::free;
					handler(result, 0, std::span<const uint8>());
				}
			}
		}
	}


	// The connection changed. While it is down a request of a FailWhileDown command is closed with noConnection; the others wait for it.
	void OnLink(bool up) {
		if constexpr (Plan::usesLink) {
			link.down = !up;
			if (!up) {
				FailDownRequests();
			}
		} else {
			(void)up;
		}
	}


	// The peer restarted: every open request is closed with reset.
	void OnRestart() {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
				if (slot.stage == RequestStage::open) {
					slot.Settle(ResultStatus::reset);
				}
			}
		}
	}


	// The outcome of a REQUEST: a failure settles its request, and a command with no reply settles it with ok. A command with a reply waits for
	// its final, and the outcome ok is only the delivery.
	void OnOutcome(const Outcome& outcome) {
		if constexpr (Plan::openCount > 0) {
			std::span<const uint8> payload = outcome.payload;
			if (payload.size() < RequestWire::RequestHeader || payload[0] != RequestWire::KindRequest) {
				return;
			}

			Slot* slot = FindOpen(GetLe16(payload.data() + 2), payload[4]);
			if (slot == nullptr) {
				return;
			}

			if (outcome.status != ResultStatus::ok || Commands::commandTable[slot->command].level == 2) {
				slot->Settle(outcome.status);
			}
		} else {
			(void)outcome;
		}
	}


	// True while the request of a number and tag is open on the requester side. Reliable asks it through DeliveryWanted.
	bool IsOpen(uint16 number, uint8 tag) const {
		if constexpr (Plan::openCount > 0) {
			for (const Slot& slot : slots) {
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


	// Takes a FINAL of one of this layer's requests. A FINAL closes its request, and its handler is called here, with the body the packet has.
	// A bad body or status closes it as invalidData; a late or duplicate final is dropped. The packet has its error size at least, and its status
	// when its code is ok.
	void TakeFinal(std::span<const uint8> packet) {
		if constexpr (Plan::openCount > 0) {
			Slot* slot = FindOpen(GetLe16(packet.data() + 1), packet[3]);
			if (slot != nullptr) {
				uint8 code = packet[4];
				ResultStatus result = ResultStatus::ok;
				uint8 status = 0;
				std::span<const uint8> body;
				if (code != RequestWire::CodeOk) {
					result = RequestWire::ResultOfCode(code);
				} else if (packet[5] >= RequestWire::StatusReservedFrom) {
					result = ResultStatus::invalidData;
				} else {
					status = packet[5];
					body = packet.subspan(RequestWire::FinalHeader);
				}

				slot->Close(result, status, body);
			}
		} else {
			(void)packet;
		}
	}


	// Takes an INTERIM of an open request: the index must rise, and the status must be a user status. The handler is called here, at once,
	// so no reply waits in a queue. The deadline of the request is re-armed in Process. The packet has its size at least.
	ResultStatus TakeInterim(std::span<const uint8> packet) {
		if constexpr (Plan::openCount > 0 && Plan::anyInterim) {
			Slot* slot = FindOpen(GetLe16(packet.data() + 1), packet[3]);
			uint8 index = packet[4];
			uint8 status = packet[5];
			if (slot == nullptr) {
				return ResultStatus::ok;
			}

			if (status >= RequestWire::StatusReservedFrom) {
				return ResultStatus::invalidData;
			}

			if (index == 0 || index <= slot->interim.lastIndex) {
				return ResultStatus::ok;
			}

			slot->interim.lastIndex = index;
			slot->armNeeded = true;
			void (*handler)(uint8) = slot->interim.interimFn;
			if (handler != nullptr) {
				handler(status);
			}
		}

		(void)packet;
		return ResultStatus::ok;
	}


private:
	// The command index of the command with the id Id, in the table of the set. The id is unique in the set, so its value finds the command.
	template<auto Id>
	static constexpr std::size_t CommandIndexOf() {
		constexpr uint8 value = static_cast<uint8>(Id);
		for (std::size_t index = 0; index < Commands::commandCount; index++) {
			if (Commands::commandTable[index].id == value) {
				return index;
			}
		}

		return Commands::commandCount;
	}


	static constexpr uint16 Successor(uint16 number) {
		return number == 0xFFFF ? static_cast<uint16>(1) : static_cast<uint16>(number + 1);
	}


	// The request of a command with a reply, from its encoded body in the packet's bytes after the header: a free slot, the next number and tag,
	// the packet sent, and the slot filled. A refusal of the send returns at once with no slot taken and no number used. The slot keeps the
	// command's index, for its deadline.
	Result<RequestId> Launch(
		std::size_t commandIndex,
		uint8 id,
		std::array<uint8, requestBytes>& packet,
		std::size_t bodyLength,
		RequestFinalFn finalFn,
		RequestInterimFn interimFn,
		const Outbox& outbox)
	{
		Slot* slot = FreeRequest();
		if (slot == nullptr) {
			return ResultStatus::busy;
		}

		uint16 number = FreeNumber();
		uint8 tag = numbers.nextTag;
		WriteHeader(id, number, tag, packet);
		std::size_t length = RequestWire::RequestHeader + bodyLength;
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
		slot->finalFn = finalFn;
		if constexpr (Plan::anyInterim) {
			slot->interim.lastIndex = 0;
			slot->interim.interimFn = interimFn;
		}

		Advance(number);
		return RequestId{number};
	}


	// The packet of a command with no reply: no slot, and the number and tag are used only when the packet is sent.
	ResultStatus LaunchFire(uint8 id, std::array<uint8, requestBytes>& packet, std::size_t bodyLength, const Outbox& outbox) {
		uint16 number = FreeNumber();
		uint8 tag = numbers.nextTag;
		WriteHeader(id, number, tag, packet);
		std::size_t length = RequestWire::RequestHeader + bodyLength;
		Meta meta{};
		meta.flags = Meta::NoAcknowledgeFlag;
		ResultStatus status = outbox.Send(std::span<const uint8>(packet.data(), length), meta);
		if (status == ResultStatus::ok) {
			Advance(number);
		}

		return status;
	}


	// Moves past a number that was sent. The tag advances with the number, so a number that is used again has another tag.
	void Advance(uint16 number) {
		numbers.nextNumber = Successor(number);
		numbers.nextTag++;
	}


	// Writes the header of a REQUEST packet: the kind, the command id, the number and the tag. The body is already after it.
	static void WriteHeader(uint8 id, uint16 number, uint8 tag, std::array<uint8, requestBytes>& packet) {
		packet[0] = RequestWire::KindRequest;
		packet[1] = id;
		PutLe16(packet.data() + 2, number);
		packet[4] = tag;
	}


	// A number that no open request has, and that is not 0. The counter moves past it only when the request is sent.
	uint16 FreeNumber() const {
		uint16 candidate = numbers.nextNumber;
		while (candidate == 0 || NumberOpen(candidate)) {
			candidate = Successor(candidate);
		}

		return candidate;
	}


	bool NumberOpen(uint16 number) const {
		if constexpr (Plan::openCount > 0) {
			for (const Slot& slot : slots) {
				if (slot.stage == RequestStage::open && slot.number == number) {
					return true;
				}
			}
		} else {
			(void)number;
		}

		return false;
	}


	Slot* FreeRequest() {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
				if (slot.stage == RequestStage::free) {
					return &slot;
				}
			}
		}

		return nullptr;
	}


	// The open request of a number and tag: only an open one (one that is not closed) is returned.
	Slot* FindOpen(uint16 number, uint8 tag) {
		if constexpr (Plan::openCount > 0) {
			for (Slot& slot : slots) {
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


	uint32 TimeoutUsOf(uint8 command) const {
		return Commands::commandTable[command].replyTimeoutMs * 1000u;
	}


	void FailDownRequests() {
		if constexpr (Plan::usesLink) {
			for (Slot& slot : slots) {
				if (slot.stage == RequestStage::open && Commands::commandTable[slot.command].failWhileDown) {
					slot.Settle(ResultStatus::noConnection);
				}
			}
		}
	}


	// The handler of a final: the result and, for a reply, the status and the body, decoded from the bytes of the packet.
	template<auto Id, auto OnFinal>
	static void RunFinal(ResultStatus result, uint8 status, std::span<const uint8> body) {
		using CommandType = CommandOf<Commands, Id>;
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


	template<auto Id, auto OnInterim>
	static void RunInterim(uint8 status) {
		using CommandType = CommandOf<Commands, Id>;
		Progress<typename CommandType::StatusType> progress{};
		progress.status = static_cast<typename CommandType::StatusType>(status);
		OnInterim(progress);
	}
};

}
