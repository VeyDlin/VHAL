#pragma once
#include <cstddef>


// A list of values of different types, as a plain recursive aggregate. The builder keeps its items in one, and so does each command
// for its arguments. std::tuple is not used for values: GCC 15 cannot evaluate a std::tuple made by a lambda called through a
// function pointer in a constant expression, which is how CommandSet<+[] {...}> evaluates its chain. Type lists still use std::tuple,
// since those are only types.


template<typename... Ts>
struct CommandPack;


template<>
struct CommandPack<> {
};


template<typename Head, typename... Tail>
struct CommandPack<Head, Tail...> {
	Head head{};
	CommandPack<Tail...> tail{};
};


// The type of element Index of a pack.
template<std::size_t Index, typename... Ts>
struct CommandPackElement;


template<typename Head, typename... Tail>
struct CommandPackElement<0, Head, Tail...> {
	using Type = Head;
};


template<std::size_t Index, typename Head, typename... Tail>
struct CommandPackElement<Index, Head, Tail...> {
	using Type = typename CommandPackElement<Index - 1, Tail...>::Type;
};


// Element Index of a pack.
template<std::size_t Index, typename Head, typename... Tail>
constexpr const typename CommandPackElement<Index, Head, Tail...>::Type& CommandPackGet(const CommandPack<Head, Tail...>& pack) {
	if constexpr (Index == 0) {
		return pack.head;
	} else {
		return CommandPackGet<Index - 1>(pack.tail);
	}
}


// Calls visitor with every element, in order. The mutable overloads give the visitor elements it can change.
template<typename Visitor>
constexpr void CommandPackForEach(const CommandPack<>&, Visitor&&) {
}


template<typename Visitor>
constexpr void CommandPackForEach(CommandPack<>&, Visitor&&) {
}


template<typename Visitor, typename Head, typename... Tail>
constexpr void CommandPackForEach(const CommandPack<Head, Tail...>& pack, Visitor&& visitor) {
	visitor(pack.head);
	CommandPackForEach(pack.tail, visitor);
}


template<typename Visitor, typename Head, typename... Tail>
constexpr void CommandPackForEach(CommandPack<Head, Tail...>& pack, Visitor&& visitor) {
	visitor(pack.head);
	CommandPackForEach(pack.tail, visitor);
}


// A pack of the given values, in order.
constexpr CommandPack<> CommandPackOf() {
	return CommandPack<>{};
}


template<typename Head, typename... Tail>
constexpr CommandPack<Head, Tail...> CommandPackOf(const Head& head, const Tail&... tail) {
	return CommandPack<Head, Tail...>{head, CommandPackOf(tail...)};
}


// A new pack with item added at the end.
template<typename New>
constexpr CommandPack<New> CommandPackAppend(const CommandPack<>&, const New& item) {
	return CommandPack<New>{item, CommandPack<>{}};
}


template<typename Head, typename... Tail, typename New>
constexpr CommandPack<Head, Tail..., New> CommandPackAppend(const CommandPack<Head, Tail...>& pack, const New& item) {
	return CommandPack<Head, Tail..., New>{pack.head, CommandPackAppend(pack.tail, item)};
}
