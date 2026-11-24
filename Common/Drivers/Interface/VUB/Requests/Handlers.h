#pragma once
#include <Drivers/Interface/VUB/Requests/Body.h>
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <Utilities/TextLiteral.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


namespace VUB {


// The ticket of a request is the layer's type (the layer is built in the next wave). It is declared here only, so that the signature
// checks can name it: a ticket handler takes VUB::Ticket<Commands, Name> by value, and the layer defines the class.
template<typename Commands, TextLiteral Name>
class Ticket;


// The final of a request on the requester side: the result of the request, and, for a reply, the status and the body. result is ok
// for a reply that arrived; any other value is a local or a refusal outcome.
template<typename StatusT, typename BodyT>
struct Final {
	ResultStatus result = ResultStatus::ok;
	StatusT status{};
	BodyT body{};
};


// An interim reply on the requester side: the status the responder sent, and result notSupported for a refused cancel.
template<typename StatusT>
struct Progress {
	ResultStatus result = ResultStatus::ok;
	StatusT status{};
};


// The kinds of handler in a table.
enum class HandlerKind : uint8 {
	on,
	onCancel,
	onEvent
};


// A ticket handler of a command with a reply: void(Ticket, const Body&), or void(Ticket) when the command has no request body.
template<TextLiteral Name, auto Fn>
struct On {
	static constexpr HandlerKind kind = HandlerKind::on;
	static constexpr std::string_view name = Name.View();
	static constexpr decltype(Fn) fn = Fn;
};


// The cancel handler of a cancellable command: void(Ticket).
template<TextLiteral Name, auto Fn>
struct OnCancel {
	static constexpr HandlerKind kind = HandlerKind::onCancel;
	static constexpr std::string_view name = Name.View();
	static constexpr decltype(Fn) fn = Fn;
};


// The handler of an event: void(const Body&), or void() when the event has no body.
template<TextLiteral Name, auto Fn>
struct OnEvent {
	static constexpr HandlerKind kind = HandlerKind::onEvent;
	static constexpr std::string_view name = Name.View();
	static constexpr decltype(Fn) fn = Fn;
};


// The handlers of a device or a PC, in a list. The function pointer of each entry is the entry's template argument: the static type
// of the entry carries it.
template<typename... Entry>
struct Handlers {
	using List = std::tuple<Entry...>;
};


// The index of the handler of Kind for Name in a tuple of entries, or the size of the tuple when there is none.
template<HandlerKind Kind, TextLiteral Name, typename Tuple, std::size_t... Index>
consteval std::size_t HandlerIndexOf(std::index_sequence<Index...>) {
	constexpr std::array<bool, sizeof...(Index)> matches = {
		(std::tuple_element_t<Index, Tuple>::kind == Kind && std::tuple_element_t<Index, Tuple>::name == Name.View())...
	};

	for (std::size_t index = 0; index < matches.size(); index++) {
		if (matches[index]) {
			return index;
		}
	}

	return matches.size();
}


// Whether the handler list of HandlerSet has a handler of Kind for Name.
template<typename HandlerSet, HandlerKind Kind, TextLiteral Name>
consteval bool HasHandler() {
	using Tuple = typename HandlerSet::List;
	return HandlerIndexOf<Kind, Name, Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>()) < std::tuple_size_v<Tuple>;
}


// The entry of the handler of Kind for Name. The entry's Fn is the handler.
template<typename HandlerSet, HandlerKind Kind, TextLiteral Name>
struct HandlerEntryOf {
	using Tuple = typename HandlerSet::List;
	static constexpr std::size_t index = HandlerIndexOf<Kind, Name, Tuple>(std::make_index_sequence<std::tuple_size_v<Tuple>>());
	static_assert(index < std::tuple_size_v<Tuple>, "the handler list has no handler of this kind for this name");
	using Type = std::tuple_element_t<(index < std::tuple_size_v<Tuple> ? index : 0), Tuple>;
};


// The check of one handler of a list, for the command set Set. Each check is a static_assert with its message.
template<typename Set, typename EntryT>
struct CheckEntry;


template<typename Set, TextLiteral Name, auto Fn>
constexpr bool CheckOn() {
	using CommandT = CommandOf<Set, Name>;
	using BodyT = typename CommandT::BodyType;
	using TicketT = Ticket<Set, Name>;

	if constexpr (CommandT::level >= 3) {
		static_assert(CommandInfoOf<Set, Name>().ticketTimeoutMs > 0, "an On handler of a command with a reply takes a ticket: the command needs TicketTimeoutMs");

		if constexpr (std::is_same_v<BodyT, NoBody>) {
			static_assert(std::is_same_v<decltype(Fn), void (*)(TicketT)>, "an On handler of a command with a reply and no request body is void(Ticket)");
		} else {
			static_assert(std::is_same_v<decltype(Fn), void (*)(TicketT, const BodyT&)>, "an On handler of a command with a reply is void(Ticket, const Body&)");
		}
	} else {
		if constexpr (std::is_same_v<BodyT, NoBody>) {
			static_assert(std::is_same_v<decltype(Fn), void (*)()>, "an On handler of a NoReply or Fire command with no request body is void()");
		} else {
			static_assert(std::is_same_v<decltype(Fn), void (*)(const BodyT&)>, "an On handler of a NoReply or Fire command takes the body only: void(const Body&)");
		}
	}

	return true;
}


template<typename Set, TextLiteral Name, auto Fn>
constexpr bool CheckOnCancel() {
	using TicketT = Ticket<Set, Name>;
	static_assert(CommandInfoOf<Set, Name>().cancellable, "an OnCancel handler names a command that is not Cancellable()");
	static_assert(std::is_same_v<decltype(Fn), void (*)(TicketT)>, "an OnCancel handler is void(Ticket)");
	return true;
}


template<typename Set, TextLiteral Name, auto Fn>
constexpr bool CheckOnEvent() {
	using BodyT = typename EventOf<Set, Name>::BodyType;

	if constexpr (std::is_same_v<BodyT, NoBody>) {
		static_assert(std::is_same_v<decltype(Fn), void (*)()>, "an OnEvent handler of an event with no body is void()");
	} else {
		static_assert(std::is_same_v<decltype(Fn), void (*)(const BodyT&)>, "an OnEvent handler is void(const Body&)");
	}

	return true;
}


template<typename Set, TextLiteral Name, auto Fn>
struct CheckEntry<Set, On<Name, Fn>> {
	static constexpr bool passed = CheckOn<Set, Name, Fn>();
};


template<typename Set, TextLiteral Name, auto Fn>
struct CheckEntry<Set, OnCancel<Name, Fn>> {
	static constexpr bool passed = CheckOnCancel<Set, Name, Fn>();
};


template<typename Set, TextLiteral Name, auto Fn>
struct CheckEntry<Set, OnEvent<Name, Fn>> {
	static constexpr bool passed = CheckOnEvent<Set, Name, Fn>();
};


template<typename Set, typename... Entry>
constexpr bool CheckEntries(std::tuple<Entry...>) {
	return (CheckEntry<Set, Entry>::passed && ...);
}


// True when no two entries of the list have the same kind and name.
template<typename... Entry>
constexpr bool NoDuplicateHandlers(std::tuple<Entry...>) {
	constexpr std::array<HandlerKind, sizeof...(Entry)> kinds = {Entry::kind...};
	constexpr std::array<std::string_view, sizeof...(Entry)> names = {Entry::name...};

	for (std::size_t first = 0; first < kinds.size(); first++) {
		for (std::size_t second = first + 1; second < kinds.size(); second++) {
			if (kinds[first] == kinds[second] && names[first] == names[second]) {
				return false;
			}
		}
	}

	return true;
}


// The checks of a handler list against a command set: every name is a command or event of the set, each handler has the signature of
// its kind, and no two handlers of one kind name the same command. The layer calls this with its set and its handlers; it is a
// constant expression, so a failed check is a static_assert with its message, at the call.
template<typename Set, typename HandlerSet>
constexpr bool CheckHandlers() {
	using Tuple = typename HandlerSet::List;
	static_assert(NoDuplicateHandlers(Tuple{}), "two handlers of one kind name the same command or event");
	return CheckEntries<Set>(Tuple{});
}


// The checks of a requester's final handler for a Send of Name: void (*)(const Final<Status, Body>&) for a command with a reply, and
// void (*)(ResultStatus) for a NoReply command. A Fire command has no final.
template<typename Set, TextLiteral Name, auto Fn>
constexpr bool CheckFinal() {
	using CommandT = CommandOf<Set, Name>;

	if constexpr (CommandT::level == 1) {
		static_assert(AlwaysFalse<Set>, "a Fire command has no final: Fire() is sent without a reply");
	} else if constexpr (CommandT::level == 2) {
		static_assert(std::is_same_v<decltype(Fn), void (*)(ResultStatus)>, "the final of a NoReply command is void(ResultStatus)");
	} else {
		using FinalT = Final<typename CommandT::StatusType, typename CommandT::ReplyType>;
		static_assert(std::is_same_v<decltype(Fn), void (*)(const FinalT&)>, "the final of a command with a reply is void(const Final<Status, Reply>&)");
	}

	return true;
}


// The checks of a requester's interim handler: void (*)(const Progress<Status>&), for a command with Interim() or Cancellable().
template<typename Set, TextLiteral Name, auto Fn>
constexpr bool CheckProgress() {
	using CommandT = CommandOf<Set, Name>;
	static_assert(CommandInfoOf<Set, Name>().interim || CommandInfoOf<Set, Name>().cancellable, "an interim handler names a command that is not Interim() or Cancellable()");
	static_assert(std::is_same_v<decltype(Fn), void (*)(const Progress<typename CommandT::StatusType>&)>, "an interim handler is void(const Progress<Status>&)");
	return true;
}


}
