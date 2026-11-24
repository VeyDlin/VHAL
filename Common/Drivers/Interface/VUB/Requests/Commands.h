#pragma once
#include <Drivers/Interface/VUB/Require.h>
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Utilities/DataTypes.h>
#include <Utilities/TextLiteral.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


namespace VUB {


// The reply declarations of a command. Reply<Status>() and Reply<Status, Body>() carry a status in the final; NoReply() carries the
// delivery outcome only (level 2); Fire() carries nothing (level 1). UndeclaredReply is the state with none of them.
template<typename StatusT, typename BodyT>
struct ReplyShape {
};


struct NoReplyShape {
};


struct FireShape {
};


struct UndeclaredReply {
};


// The status of a command with no status in its final.
struct NoStatus {
};


// The body of an event that has no Body<T>() declaration yet.
struct UndeclaredBody {
};


// The levels of a reply declaration: 0 is not declared, 1 is Fire(), 2 is NoReply(), 3 is Reply<Status>(). A command with Interim() or
// Cancellable() is level 4 (the level is computed by CommandSpec).
template<typename ReplyT>
struct ReplyTraits {
	static constexpr uint8 level = 0;
	using Status = NoStatus;
	using Body = NoBody;
};


template<typename StatusT, typename BodyT>
struct ReplyTraits<ReplyShape<StatusT, BodyT>> {
	static constexpr uint8 level = 3;
	using Status = StatusT;
	using Body = BodyT;
};


template<>
struct ReplyTraits<NoReplyShape> {
	static constexpr uint8 level = 2;
	using Status = NoStatus;
	using Body = NoBody;
};


template<>
struct ReplyTraits<FireShape> {
	static constexpr uint8 level = 1;
	using Status = NoStatus;
	using Body = NoBody;
};


// True for an enum class whose underlying type is uint8: the only type a reply status can have.
template<typename T, bool IsEnum = std::is_enum_v<T>>
struct IsStatusEnum : std::false_type {
};


template<typename T>
struct IsStatusEnum<T, true> : std::bool_constant<std::is_same_v<std::underlying_type_t<T>, uint8>> {
};


// The options of a command. Each builder call sets one of them; a value that is not set keeps its default.
struct CommandOptions {
	bool interim = false;
	bool cancellable = false;
	bool failWhileDown = false;
	uint32 replyTimeoutMs = 0;
	uint32 ticketTimeoutMs = 0;
};


// The options of an event.
struct EventOptions {
	bool delivered = false;
};


// The information of one command, as the layer reads it at run time. The pointers are to the static views of the bodies.
struct CommandInfo {
	std::string_view name{};
	const BodyView* request = nullptr;
	const BodyView* reply = nullptr;
	uint32 replyTimeoutMs = 0;
	uint32 ticketTimeoutMs = 0;
	uint8 id = 0;
	uint8 level = 0;
	bool interim = false;
	bool cancellable = false;
	bool failWhileDown = false;
};


// The information of one event, as the layer reads it at run time.
struct EventInfo {
	std::string_view name{};
	const BodyView* body = nullptr;
	uint8 id = 0;
	bool delivered = false;
};


// A command in the description. The name and id are template arguments; the options are a value, set by the builder's calls.
template<TextLiteral Name, uint32 Id, typename BodyT, typename ReplyT>
struct CommandSpec {
	static constexpr bool isCommand = true;
	static constexpr std::string_view name = Name.View();
	static constexpr uint8 id = static_cast<uint8>(Id);
	static constexpr uint8 level = ReplyTraits<ReplyT>::level;
	using BodyType = BodyT;
	using ReplyType = typename ReplyTraits<ReplyT>::Body;
	using StatusType = typename ReplyTraits<ReplyT>::Status;
	CommandOptions options{};


	constexpr CommandInfo Info() const {
		CommandInfo info;
		info.name = name;
		info.id = id;
		info.level = level;

		if (level == 3 && (options.interim || options.cancellable)) {
			info.level = 4;
		}

		info.request = &BodyViewOf<BodyT>;
		info.reply = &BodyViewOf<ReplyType>;
		info.replyTimeoutMs = options.replyTimeoutMs;
		info.ticketTimeoutMs = options.ticketTimeoutMs;
		info.interim = options.interim;
		info.cancellable = options.cancellable;
		info.failWhileDown = options.failWhileDown;
		return info;
	}
};


// An event in the description.
template<TextLiteral Name, uint32 Id, typename BodyT>
struct EventSpec {
	static constexpr bool isCommand = false;
	static constexpr std::string_view name = Name.View();
	static constexpr uint8 id = static_cast<uint8>(Id);
	using BodyType = BodyT;
	EventOptions options{};


	constexpr EventInfo Info() const {
		EventInfo info;
		info.name = name;
		info.id = id;
		info.body = &BodyViewOf<BodyT>;
		info.delivered = options.delivered;
		return info;
	}
};


// The closed items of a description, in order. A chain is a value: each item is a type, and the options are its value members.
struct ChainEnd {
};


template<typename PrevT, typename ItemT>
struct ChainNode {
	using Last = ItemT;
	PrevT prev{};
	ItemT item{};
};


// The types of the items of a chain, in order, as a tuple of types.
template<typename ChainT>
struct ChainItems {
	using Type = std::tuple<>;
};


template<typename Tuple, typename ItemT>
struct AppendItemType;


template<typename... Items, typename ItemT>
struct AppendItemType<std::tuple<Items...>, ItemT> {
	using Type = std::tuple<Items..., ItemT>;
};


template<typename PrevT, typename ItemT>
struct ChainItems<ChainNode<PrevT, ItemT>> {
	using Type = typename AppendItemType<typename ChainItems<PrevT>::Type, ItemT>::Type;
};


template<typename ChainT, TextLiteral Name, uint32 Id, typename BodyT, typename ReplyT>
struct OpenCommand;


template<typename ChainT, TextLiteral Name, uint32 Id, typename BodyT>
struct OpenEvent;


// The builder between two items: the closed items so far, and nothing open. Commands() gives one with no items.
template<typename ChainT>
struct CommandsBuilder {
	ChainT chain{};


	template<TextLiteral Name, uint32 Id>
	constexpr OpenCommand<ChainT, Name, Id, NoBody, UndeclaredReply> Command() const {
		static_assert(Id < 0xF0, "a command id is 0xF0 or more: the ids 0xF0 to 0xFF are reserved for the layer");
		return {chain, CommandOptions{}};
	}


	template<TextLiteral Name, uint32 Id>
	constexpr OpenEvent<ChainT, Name, Id, UndeclaredBody> Event() const {
		static_assert(Id < 0xF0, "an event id is 0xF0 or more: the ids 0xF0 to 0xFF are reserved for the layer");
		return {chain, EventOptions{}};
	}


	constexpr CommandsBuilder Close() const {
		return *this;
	}
};


// A command being declared: its name, id, request body and reply are set; the options follow. The next Command(), Event() or the end of
// the description closes it.
template<typename ChainT, TextLiteral Name, uint32 Id, typename BodyT, typename ReplyT>
struct OpenCommand {
	ChainT chain{};
	CommandOptions options{};


	template<typename NewBodyT>
	constexpr OpenCommand<ChainT, Name, Id, NewBodyT, ReplyT> Request() const {
		static_assert(HasBodyOf<NewBodyT>, "a request body type has no BodyOf: give the type a BodyOf specialization with Layout()");
		return {chain, options};
	}


	template<typename StatusT, typename ReplyBodyT = NoBody>
	constexpr OpenCommand<ChainT, Name, Id, BodyT, ReplyShape<StatusT, ReplyBodyT>> Reply() const {
		static_assert(IsStatusEnum<StatusT>::value, "a reply status is an enum class with a uint8 underlying type");
		static_assert(HasBodyOf<ReplyBodyT>, "a reply body type has no BodyOf: give the type a BodyOf specialization with Layout()");
		static_assert(ReplyTraits<ReplyT>::level == 0, "a command has one reply declaration: Reply, NoReply or Fire");
		return {chain, options};
	}


	constexpr OpenCommand<ChainT, Name, Id, BodyT, NoReplyShape> NoReply() const {
		static_assert(ReplyTraits<ReplyT>::level == 0, "a command has one reply declaration: Reply, NoReply or Fire");
		return {chain, options};
	}


	constexpr OpenCommand<ChainT, Name, Id, BodyT, FireShape> Fire() const {
		static_assert(ReplyTraits<ReplyT>::level == 0, "a command has one reply declaration: Reply, NoReply or Fire");
		return {chain, options};
	}


	constexpr OpenCommand Interim() const {
		static_assert(ReplyTraits<ReplyT>::level == 3, "Interim() needs a Reply<Status>() earlier in the chain: interim replies carry a status");
		OpenCommand next = *this;
		next.options.interim = true;
		return next;
	}


	constexpr OpenCommand Cancellable() const {
		static_assert(ReplyTraits<ReplyT>::level == 3, "Cancellable() is for a command with a Reply<Status>() earlier in the chain");
		OpenCommand next = *this;
		next.options.cancellable = true;
		return next;
	}


	constexpr OpenCommand ReplyTimeoutMs(uint32 ms) const {
		Require<"ReplyTimeoutMs must be above 0">(ms > 0);
		OpenCommand next = *this;
		next.options.replyTimeoutMs = ms;
		return next;
	}


	constexpr OpenCommand TicketTimeoutMs(uint32 ms) const {
		Require<"TicketTimeoutMs must be above 0">(ms > 0);
		OpenCommand next = *this;
		next.options.ticketTimeoutMs = ms;
		return next;
	}


	constexpr OpenCommand FailWhileDown() const {
		OpenCommand next = *this;
		next.options.failWhileDown = true;
		return next;
	}


	constexpr CommandsBuilder<ChainNode<ChainT, CommandSpec<Name, Id, BodyT, ReplyT>>> Close() const {
		static_assert(ReplyTraits<ReplyT>::level != 0, "a command needs Reply<Status>(), NoReply() or Fire() after its Request()");
		using Item = CommandSpec<Name, Id, BodyT, ReplyT>;
		using Node = ChainNode<ChainT, Item>;
		return CommandsBuilder<Node>{Node{chain, Item{options}}};
	}


	template<TextLiteral NextName, uint32 NextId>
	constexpr auto Command() const {
		return Close().template Command<NextName, NextId>();
	}


	template<TextLiteral NextName, uint32 NextId>
	constexpr auto Event() const {
		return Close().template Event<NextName, NextId>();
	}
};


// An event being declared. Body<T>() gives its body, Delivered() its delivery.
template<typename ChainT, TextLiteral Name, uint32 Id, typename BodyT>
struct OpenEvent {
	ChainT chain{};
	EventOptions options{};


	template<typename NewBodyT>
	constexpr OpenEvent<ChainT, Name, Id, NewBodyT> Body() const {
		static_assert(HasBodyOf<NewBodyT>, "an event body type has no BodyOf: give the type a BodyOf specialization with Layout()");
		return {chain, options};
	}


	constexpr OpenEvent Delivered() const {
		OpenEvent next = *this;
		next.options.delivered = true;
		return next;
	}


	constexpr CommandsBuilder<ChainNode<ChainT, EventSpec<Name, Id, BodyT>>> Close() const {
		static_assert(!std::is_same_v<BodyT, UndeclaredBody>, "an event has no Body<T>(): declare Body<VUB::NoBody>() for an event with no body");
		using Item = EventSpec<Name, Id, BodyT>;
		using Node = ChainNode<ChainT, Item>;
		return CommandsBuilder<Node>{Node{chain, Item{options}}};
	}


	template<TextLiteral NextName, uint32 NextId>
	constexpr auto Command() const {
		return Close().template Command<NextName, NextId>();
	}


	template<TextLiteral NextName, uint32 NextId>
	constexpr auto Event() const {
		return Close().template Event<NextName, NextId>();
	}
};


// The start of a description: no commands and no events.
consteval CommandsBuilder<ChainEnd> Commands() {
	return {};
}


// The number of commands, and of events, in a chain.
template<typename ChainT>
constexpr std::size_t CountCommandItems(const ChainT& chain) {
	if constexpr (std::is_same_v<ChainT, ChainEnd>) {
		return 0;
	} else {
		using ItemT = typename ChainT::Last;
		return CountCommandItems(chain.prev) + (ItemT::isCommand ? 1 : 0);
	}
}


template<typename ChainT>
constexpr std::size_t CountEventItems(const ChainT& chain) {
	if constexpr (std::is_same_v<ChainT, ChainEnd>) {
		return 0;
	} else {
		using ItemT = typename ChainT::Last;
		return CountEventItems(chain.prev) + (ItemT::isCommand ? 0 : 1);
	}
}


// Writes the command infos of a chain into table, in declaration order.
template<typename ChainT, std::size_t Count>
constexpr void FillCommands(const ChainT& chain, std::array<CommandInfo, Count>& table, std::size_t& index) {
	if constexpr (!std::is_same_v<ChainT, ChainEnd>) {
		FillCommands(chain.prev, table, index);

		if constexpr (ChainT::Last::isCommand) {
			table[index] = chain.item.Info();
			index++;
		}
	}
}


template<typename ChainT, std::size_t Count>
constexpr void FillEvents(const ChainT& chain, std::array<EventInfo, Count>& table, std::size_t& index) {
	if constexpr (!std::is_same_v<ChainT, ChainEnd>) {
		FillEvents(chain.prev, table, index);

		if constexpr (!ChainT::Last::isCommand) {
			table[index] = chain.item.Info();
			index++;
		}
	}
}


template<std::size_t Count, typename ChainT>
constexpr std::array<CommandInfo, Count> BuildCommandTable(const ChainT& chain) {
	std::array<CommandInfo, Count> table{};
	std::size_t index = 0;
	FillCommands(chain, table, index);
	return table;
}


template<std::size_t Count, typename ChainT>
constexpr std::array<EventInfo, Count> BuildEventTable(const ChainT& chain) {
	std::array<EventInfo, Count> table{};
	std::size_t index = 0;
	FillEvents(chain, table, index);
	return table;
}


// True when no two entries of an id table have the same id.
template<typename InfoT, std::size_t Count>
constexpr bool NoDuplicateIds(const std::array<InfoT, Count>& table) {
	for (std::size_t first = 0; first < Count; first++) {
		for (std::size_t second = first + 1; second < Count; second++) {
			if (table[first].id == table[second].id) {
				return false;
			}
		}
	}

	return true;
}


// True when no name is used twice, by a command or an event.
template<std::size_t CommandCount, std::size_t EventCount>
constexpr bool NoDuplicateNames(const std::array<CommandInfo, CommandCount>& commands, const std::array<EventInfo, EventCount>& events) {
	std::array<std::string_view, CommandCount + EventCount> names{};

	for (std::size_t index = 0; index < CommandCount; index++) {
		names[index] = commands[index].name;
	}

	for (std::size_t index = 0; index < EventCount; index++) {
		names[CommandCount + index] = events[index].name;
	}

	for (std::size_t first = 0; first < names.size(); first++) {
		for (std::size_t second = first + 1; second < names.size(); second++) {
			if (names[first] == names[second]) {
				return false;
			}
		}
	}

	return true;
}


template<std::size_t Count>
constexpr std::size_t LargestRequestOf(const std::array<CommandInfo, Count>& table) {
	std::size_t largest = 0;

	for (const CommandInfo& info : table) {
		if (info.request->maxSize > largest) {
			largest = info.request->maxSize;
		}
	}

	return largest;
}


template<std::size_t Count>
constexpr std::size_t LargestReplyOf(const std::array<CommandInfo, Count>& table) {
	std::size_t largest = 0;

	for (const CommandInfo& info : table) {
		if (info.reply->maxSize > largest) {
			largest = info.reply->maxSize;
		}
	}

	return largest;
}


template<std::size_t Count>
constexpr std::size_t LargestEventOf(const std::array<EventInfo, Count>& table) {
	std::size_t largest = 0;

	for (const EventInfo& info : table) {
		if (info.body->maxSize > largest) {
			largest = info.body->maxSize;
		}
	}

	return largest;
}


// The index of the item named Name in a tuple of items, or the size of the tuple when no item has the name.
template<TextLiteral Name, typename Tuple, std::size_t... Index>
consteval std::size_t ItemIndexByName(std::index_sequence<Index...>) {
	constexpr std::array<bool, sizeof...(Index)> matches = {(std::tuple_element_t<Index, Tuple>::name == Name.View())...};

	for (std::size_t index = 0; index < matches.size(); index++) {
		if (matches[index]) {
			return index;
		}
	}

	return matches.size();
}


// The item of a tuple named Name. WantCommand selects the kind: a command or an event.
template<TextLiteral Name, typename Tuple, bool WantCommand>
struct ItemByName {
	static constexpr std::size_t index = ItemIndexByName<Name, Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>());
	static_assert(index < std::tuple_size_v<Tuple>, "no command or event of the set has this name");
	using Type = std::tuple_element_t<(index < std::tuple_size_v<Tuple> ? index : 0), Tuple>;
	static_assert(Type::isCommand == WantCommand, "the name is of an event where a command is expected, or of a command where an event is expected");
};


// The set of commands and events described by a builder chain. The argument is a builder lambda, as for CommandSet in CommandLine.
template<auto (*Make)()>
class CommandSet {
public:
	static constexpr auto built = Make().Close();
	using Items = typename ChainItems<decltype(built.chain)>::Type;
	static constexpr std::size_t commandCount = CountCommandItems(built.chain);
	static constexpr std::size_t eventCount = CountEventItems(built.chain);
	static constexpr std::array<CommandInfo, commandCount> commandTable = BuildCommandTable<commandCount>(built.chain);
	static constexpr std::array<EventInfo, eventCount> eventTable = BuildEventTable<eventCount>(built.chain);
	static constexpr std::size_t largestRequest = LargestRequestOf(commandTable);
	static constexpr std::size_t largestReply = LargestReplyOf(commandTable);
	static constexpr std::size_t largestEvent = LargestEventOf(eventTable);

	static_assert(commandCount <= 240, "more than 240 commands: the command ids are one byte and 0xF0 to 0xFF are reserved");
	static_assert(eventCount <= 240, "more than 240 events: the event ids are one byte and 0xF0 to 0xFF are reserved");
	static_assert(NoDuplicateIds(commandTable), "two commands have the same id");
	static_assert(NoDuplicateIds(eventTable), "two events have the same id");
	static_assert(NoDuplicateNames(commandTable, eventTable), "two commands or events have the same name");


	// The info of the command with this id, or nullptr when the set has none. The layer's dispatch uses it.
	static constexpr const CommandInfo* CommandById(uint8 id) {
		for (const CommandInfo& info : commandTable) {
			if (info.id == id) {
				return &info;
			}
		}

		return nullptr;
	}


	static constexpr const EventInfo* EventById(uint8 id) {
		for (const EventInfo& info : eventTable) {
			if (info.id == id) {
				return &info;
			}
		}

		return nullptr;
	}
};


// The description of the command named Name in the set Set: BodyType, StatusType, ReplyType, and the id. Its flags and timeouts are in
// CommandInfoOf<Set, Name>().
template<typename Set, TextLiteral Name>
using CommandOf = typename ItemByName<Name, typename Set::Items, true>::Type;


// The description of the event named Name in the set Set: BodyType and the id.
template<typename Set, TextLiteral Name>
using EventOf = typename ItemByName<Name, typename Set::Items, false>::Type;


// The info (flags and timeouts included) of the command named Name.
template<typename Set, TextLiteral Name>
constexpr CommandInfo CommandInfoOf() {
	for (const CommandInfo& info : Set::commandTable) {
		if (info.name == Name.View()) {
			return info;
		}
	}

	Require<"no command of the set has this name">(false);
	return {};
}


// The info (delivery included) of the event named Name.
template<typename Set, TextLiteral Name>
constexpr EventInfo EventInfoOf() {
	for (const EventInfo& info : Set::eventTable) {
		if (info.name == Name.View()) {
			return info;
		}
	}

	Require<"no event of the set has this name">(false);
	return {};
}


}
