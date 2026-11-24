#pragma once
#include <Drivers/Interface/VUB/Drops.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Outbox.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Drivers/Interface/VUB/Requests/Handlers.h>
#include <Drivers/Interface/VUB/Requests/Wire.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>


namespace VUB {


// True when one event of the set is Delivered: its packets are sequenced by Reliable, so the layer needs Reliable below it.
template<typename Commands>
constexpr bool AnyEventDelivered() {
	for (const EventInfo& info : Commands::eventTable) {
		if (info.delivered) {
			return true;
		}
	}

	return false;
}


// True when the handler list has an OnEvent entry: a side with one receives events.
template<typename... Entry>
constexpr bool AnyOnEvent(std::tuple<Entry...>) {
	return (false || ... || (Entry::kind == HandlerKind::onEvent));
}


// The events of a requests layer: the emitter of the events the set declares, and the receiver of the events the handler list serves.
// Both are static, so the layer holds no state for them. The plan gives the layer's options; an event that is not used builds no code.
template<typename Commands, typename HandlerSet, typename Plan>
class EventsOf {
public:
	// The packet of an event: the kind, the id and the body. An event that is not Delivered goes with the no-acknowledgement flag, so Reliable
	// sends it unsequenced and keeps no slot for it. A Delivered event is sent sequenced, and needs Reliable below. Returns what the outbox returns.
	template<auto Id>
	static ResultStatus Emit(const typename EventOf<Commands, Id>::BodyType& body, const Outbox& outbox) {
		using BodyType = typename EventOf<Commands, Id>::BodyType;
		constexpr EventInfo info = EventInfoOf<Commands, Id>();
		static_assert(!info.delivered || Plan::ownerBelow,
			"an event with Delivered() needs Reliable below Requests: a Delivered event is sequenced by Reliable");
		constexpr std::size_t bodyLength = BodyViewOf<BodyType>.maxSize;
		std::array<uint8, RequestWire::EventHeader + bodyLength> packet{};
		packet[0] = RequestWire::KindEvent;
		packet[1] = info.id;
		(void)Encode(body, std::span<uint8>(packet.data() + RequestWire::EventHeader, bodyLength));
		Meta meta{};
		if constexpr (!info.delivered) {
			meta.flags = Meta::NoAcknowledgeFlag;
		}

		return outbox.Send(std::span<const uint8>(packet.data(), RequestWire::EventHeader + bodyLength), meta);
	}


	// Takes an EVENT packet of at least the header's size. The handler of the event runs here, so the events and the requests of one walk
	// run in arrival order. Returns ok when the packet is consumed, notFound when the id is unknown or has no handler (a refusal), and
	// invalidData when the body is shorter than the handler's body.
	static ResultStatus Take(std::span<const uint8> packet) {
		return TakeHandler<0>(packet[1], packet.subspan(RequestWire::EventHeader));
	}


	// The outcome of the packet of a Delivered event: any outcome but ok (a retry exhausted, or a reset by a restart of the peer) is a loss, and
	// counts in eventsLost. Only a Delivered event has an outcome, since the other events are sent unacknowledged.
	static void OnOutcome(const Outcome& outcome, uint16& eventsLost) {
		std::span<const uint8> payload = outcome.payload;
		if (outcome.status != ResultStatus::ok && payload.size() > 0 && payload[0] == RequestWire::KindEvent) {
			CountDrop(eventsLost);
		}
	}


private:
	// Walks the handler list for the OnEvent entry with the id, and runs it. An entry of another kind, or of another id, is passed by.
	template<std::size_t Index>
	static ResultStatus TakeHandler(uint8 id, std::span<const uint8> body) {
		using Tuple = typename HandlerSet::List;
		if constexpr (Index >= std::tuple_size_v<Tuple>) {
			(void)id;
			(void)body;
			return ResultStatus::notFound;
		} else {
			using Entry = std::tuple_element_t<Index, Tuple>;
			if constexpr (Entry::kind == HandlerKind::onEvent) {
				if (static_cast<uint8>(Entry::key) == id) {
					return RunEvent<Entry>(body);
				}
			}

			return TakeHandler<Index + 1>(id, body);
		}
	}


	// Decodes the body of an event for its handler and calls the handler. The body type comes from the event of the entry's id.
	template<typename Entry>
	static ResultStatus RunEvent(std::span<const uint8> body) {
		using BodyType = typename EventOf<Commands, Entry::key>::BodyType;
		BodyType value{};
		if (Decode(body, value) != ResultStatus::ok) {
			return ResultStatus::invalidData;
		}

		if constexpr (std::is_same_v<BodyType, NoBody>) {
			Entry::fn();
		} else {
			Entry::fn(value);
		}

		return ResultStatus::ok;
	}
};


}
