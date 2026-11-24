#pragma once
#include <Drivers/Interface/VUB/Bytes.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests/Ticket.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>


namespace VUB {


// The handlers of one On entry: the check of a request body and the call of the handler with a ticket. Defined here.
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


// The stage of a responder ticket: free, received (the body is copied, the handler not yet called), open (the handler has it), or final
// (the final is encoded and waits to be sent).
enum class TicketStage : uint8 {
	free,
	received,
	open,
	final
};


// The interim state of a responder ticket: the pending interim is its status and its index. The index counts the interims of the request.
struct TicketInterim {
	uint8 status = 0;
	uint8 index = 0;
	bool pending = false;
};


// One request on the responder side. The bytes hold the request body, or the final packet once the final is made. The interim part is a
// member only when the set has interim commands.
template<std::size_t Bytes, bool Interim>
struct TicketSlot {
	std::array<uint8, Bytes> bytes{};
	[[no_unique_address]] std::conditional_t<Interim, TicketInterim, NoStorage> interim{};
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


// The refusal ring: the refusals of the requests that the tickets could not take, sent in order from Process.
template<std::size_t Count>
struct RefusalRing {
	std::array<Refusal, Count> entries{};
	std::size_t head = 0;
	std::size_t used = 0;
};


// The responder side of the requests layer: the tickets of the requests the handlers serve, the refusal ring, and the calls of the handlers.
// The layer gives its plan, so a set with no ticket or no refusal has no storage for them. The command set and the handler list are the
// layer's.
template<typename Commands, typename HandlerSet, typename Plan>
class ResponderOf {
private:
	static constexpr std::size_t ticketCount = Plan::ticketCount;
	static constexpr std::size_t refusalCount = Plan::refusalCount;
	static constexpr std::size_t handlerCount = Plan::handlerCount;
	static constexpr bool ticketed = Plan::ticketed;
	static constexpr bool anyInterim = Plan::anyInterim;
	static constexpr std::size_t largestRequest = Commands::largestRequest;
	static constexpr std::size_t largestReply = Commands::largestReply;
	static constexpr std::size_t ticketBytes = std::max(largestRequest, RequestWire::FinalHeader + largestReply);
	static constexpr std::array<std::size_t, Commands::commandCount> onIndex = Plan::onIndex;
	using Slot = TicketSlot<ticketBytes, anyInterim>;
	std::array<Slot, ticketCount> tickets{};
	[[no_unique_address]] std::conditional_t<(refusalCount > 0), RefusalRing<refusalCount>, NoStorage> refusals{};


public:
	// The table of the ticket calls of this layer: the layer's finish, open and, with interim commands, interim.
	static const TicketOps* Operations() {
		if constexpr (anyInterim) {
			static constexpr TicketInterimOps operations = {{&FinishOp, &OpenOp}, &InterimOp};
			return &operations;
		} else {
			static constexpr TicketOps operations = {&FinishOp, &OpenOp};
			return &operations;
		}
	}


	// Takes a REQUEST, the responder's side. A request with a reply takes a ticket, or is refused from the ring; a command with no reply runs at
	// once, after the requests received earlier in the walk. Returns ok when the packet is consumed, and the error of a packet that is not.
	ResultStatus Take(std::span<const uint8> packet) {
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
			Slot* open = FindTicketByNumber(number);
			if (open != nullptr && answers) {
				if (open->tag != tag) {
					Refuse(number, tag, RequestWire::CodeBusy);
				}

				return ResultStatus::ok;
			}
		}

		uint8 code = AdmitByIndex(command, number, tag, packet.subspan(RequestWire::RequestHeader));
		if (code == RequestWire::CodeOk) {
			return ResultStatus::ok;
		}

		if (answers) {
			Refuse(number, tag, code);
			return ResultStatus::ok;
		}

		if (code == RequestWire::CodeNotSupported) {
			return ResultStatus::notSupported;
		}

		return ResultStatus::invalidData;
	}


	// Runs the handlers of the received requests, in arrival order. A received request's ticket is open after this: its deadline is armed in
	// Process. It takes no clock, so Take can call it: a command with no reply runs after the requests received earlier in the walk.
	void RunReceived() {
		if constexpr (ticketCount > 0) {
			for (std::size_t index = 0; index < ticketCount; index++) {
				Slot& slot = tickets[index];
				if (slot.stage != TicketStage::received) {
					continue;
				}

				slot.stage = TicketStage::open;
				slot.rearm = true;
				CallCommand(slot.command, static_cast<uint8>(index), slot, std::make_index_sequence<Commands::commandCount>());
			}
		}
	}


	// Arms the deadline of each ticket that was opened or given an interim since the last Process.
	void RearmTickets(uint32 nowUs) {
		if constexpr (ticketCount > 0) {
			for (Slot& slot : tickets) {
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
			for (Slot& slot : tickets) {
				if (slot.stage == TicketStage::open && RequestWire::Due(nowUs, slot.deadlineUs)) {
					WriteFinalError(slot, RequestWire::CodeTimeout);
				}
			}
		} else {
			(void)nowUs;
		}
	}


	// Sends what is waiting, in order: the refusals, then each ticket's interim and final. A busy send keeps its packet for the next Process;
	// any other result ends it.
	void SendResponses(const Outbox& outbox) {
		if constexpr (refusalCount > 0) {
			while (refusals.used > 0) {
				Refusal& refusal = refusals.entries[refusals.head];
				std::array<uint8, RequestWire::FinalErrorSize> packet{};
				packet[0] = RequestWire::KindFinal;
				PutLe16(packet.data() + 1, refusal.number);
				packet[3] = refusal.tag;
				packet[4] = refusal.code;
				if (outbox.Send(std::span<const uint8>(packet.data(), packet.size())) == ResultStatus::busy) {
					return;
				}

				refusals.head = (refusals.head + 1) % refusalCount;
				refusals.used--;
			}
		}

		for (Slot& slot : tickets) {
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


	// The restart: the tickets are freed (a final not yet sent is discarded), and the refusal ring is emptied.
	void OnRestart() {
		for (Slot& slot : tickets) {
			slot.stage = TicketStage::free;
			if constexpr (anyInterim) {
				slot.interim.pending = false;
			}
		}

		if constexpr (refusalCount > 0) {
			refusals.used = 0;
			refusals.head = 0;
		}
	}


private:
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
				Slot* slot = FreeTicket();
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


	std::size_t CommandIndexById(uint8 id) const {
		for (std::size_t index = 0; index < Commands::commandCount; index++) {
			if (Commands::commandTable[index].id == id) {
				return index;
			}
		}

		return Commands::commandCount;
	}


	Slot* FreeTicket() {
		if constexpr (ticketCount > 0) {
			for (Slot& slot : tickets) {
				if (slot.stage == TicketStage::free) {
					return &slot;
				}
			}
		}

		return nullptr;
	}


	Slot* FindTicketByNumber(uint16 number) {
		if constexpr (ticketCount > 0) {
			for (Slot& slot : tickets) {
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
			if (refusals.used == refusalCount) {
				return;
			}

			std::size_t position = (refusals.head + refusals.used) % refusalCount;
			refusals.entries[position] = Refusal{number, tag, code};
			refusals.used++;
		} else {
			(void)number;
			(void)tag;
			(void)code;
		}
	}


	template<std::size_t... Index>
	void CallCommand(uint8 command, uint8 slotIndex, Slot& slot, std::index_sequence<Index...>) {
		(void)command;
		(void)slotIndex;
		(void)slot;
		((command == Index ? (CallOne<Index>(slotIndex, slot), true) : false) || ...);
	}


	template<std::size_t Index>
	void CallOne(uint8 slotIndex, Slot& slot) {
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


	// The live ticket of a handle's call: its slot holds the generation and is open.
	bool TicketLive(uint8 index, uint8 generation) const {
		if constexpr (ticketCount > 0) {
			if (index < ticketCount) {
				const Slot& slot = tickets[index];
				return slot.stage == TicketStage::open && slot.generation == generation;
			}
		}

		return false;
	}


	ResultStatus QueueInterim(uint8 index, uint8 generation, uint8 status) {
		if constexpr (anyInterim) {
			if (!TicketLive(index, generation)) {
				return ResultStatus::notAvailable;
			}

			Slot& slot = tickets[index];
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
		if (!TicketLive(index, generation)) {
			return ResultStatus::notAvailable;
		}

		Slot& slot = tickets[index];
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
	}


	// Writes the final packet of an error code into the slot's bytes: the request's number and tag, the code, and no body.
	static void WriteFinalError(Slot& slot, uint8 code) {
		slot.bytes[0] = RequestWire::KindFinal;
		PutLe16(slot.bytes.data() + 1, slot.number);
		slot.bytes[3] = slot.tag;
		slot.bytes[4] = code;
		slot.length = RequestWire::FinalErrorSize;
		slot.stage = TicketStage::final;
	}


	static ResultStatus InterimOp(void* layer, uint8 index, uint8 generation, uint8 status) {
		ResponderOf* self = static_cast<ResponderOf*>(layer);
		return self->QueueInterim(index, generation, status);
	}


	static ResultStatus FinishOp(void* layer, uint8 index, uint8 generation, uint8 status, std::span<const uint8> body) {
		ResponderOf* self = static_cast<ResponderOf*>(layer);
		return self->QueueFinal(index, generation, status, body);
	}


	static bool OpenOp(const void* layer, uint8 index, uint8 generation) {
		const ResponderOf* self = static_cast<const ResponderOf*>(layer);
		return self->TicketLive(index, generation);
	}
};

}
