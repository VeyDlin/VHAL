#pragma once
#include "AtArgument.h"
#include "AtError.h"
#include <Utilities/CommandLine/CommandPack.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>


// The forms of a command and the items of a set. A form is a type that names its kind, and its values (a read name, an argument list)
// are plain values, so the set keeps both the types and the declared values. A command item is a name, its forms and its options.


// The kinds of form. Test, Read, Write and Run are the forms of a command, and Value is the form of a basic command.
enum class AtFormKind : uint8 {
	test,
	read,
	write,
	run,
	value
};


// The number of form kinds. A command has one flag for each.
inline constexpr std::size_t AtFormKindCount = 5;
static_assert(AtFormKindCount == static_cast<std::size_t>(AtFormKind::value) + 1, "AtFormKindCount is out of step with AtFormKind");


// AT+X=?
struct AtTestForm {
	static constexpr AtFormKind kind = AtFormKind::test;
};


// AT+X?, with one value of type T, which the info line carries.
template<typename T>
struct AtReadForm {
	using Type = T;
	static constexpr AtFormKind kind = AtFormKind::read;
	std::string_view name{};
};


// AT+X=a,b
template<typename... Arguments>
struct AtWriteForm {
	static constexpr AtFormKind kind = AtFormKind::write;
	CommandPack<Arguments...> arguments{};
};


// AT+X
struct AtRunForm {
	static constexpr AtFormKind kind = AtFormKind::run;
};


// ATE0: the value of a basic command follows its letter.
template<typename... Arguments>
struct AtValueForm {
	static constexpr AtFormKind kind = AtFormKind::value;
	CommandPack<Arguments...> arguments{};
};


// The most final lines a command can declare, of each kind.
constexpr std::size_t AtMaxFinals = 4;


// The options of a command. A zero timeout means the set's default, and an empty reply prefix means the name followed by a colon.
struct AtCommandOptions {
	uint32 timeoutMs = 0;
	std::string_view replyPrefix{};
	std::array<std::string_view, AtMaxFinals> finals{};
	std::size_t finalCount = 0;
	std::array<std::string_view, AtMaxFinals> failures{};
	std::size_t failureCount = 0;
};


// The reply value of a read form in a list, or void when the list has none.
template<typename... Forms>
struct AtReadValueOf {
	using Type = void;
};


template<typename T, typename... Tail>
struct AtReadValueOf<AtReadForm<T>, Tail...> {
	using Type = T;
};


template<typename Head, typename... Tail>
struct AtReadValueOf<Head, Tail...> : AtReadValueOf<Tail...> {
};


// The argument types of a write form in a list as a tuple, or an empty tuple when the list has none.
template<typename... Forms>
struct AtWriteArgsOf {
	using Type = std::tuple<>;
};


template<typename... Arguments, typename... Tail>
struct AtWriteArgsOf<AtWriteForm<Arguments...>, Tail...> {
	using Type = std::tuple<Arguments...>;
};


template<typename Head, typename... Tail>
struct AtWriteArgsOf<Head, Tail...> : AtWriteArgsOf<Tail...> {
};


// The argument types of a value form in a list as a tuple, or an empty tuple when the list has none.
template<typename... Forms>
struct AtValueArgsOf {
	using Type = std::tuple<>;
};


template<typename... Arguments, typename... Tail>
struct AtValueArgsOf<AtValueForm<Arguments...>, Tail...> {
	using Type = std::tuple<Arguments...>;
};


template<typename Head, typename... Tail>
struct AtValueArgsOf<Head, Tail...> : AtValueArgsOf<Tail...> {
};


// The index of a form kind in a per-kind table.
constexpr std::size_t AtFormIndex(AtFormKind kind) {
	return static_cast<std::size_t>(kind);
}


// The number of forms of one kind in a list.
template<AtFormKind Kind, typename... Forms>
constexpr std::size_t AtFormCount() {
	std::array<AtFormKind, sizeof...(Forms)> kinds{Forms::kind...};
	std::size_t count = 0;

	for (AtFormKind kind : kinds) {
		if (kind == Kind) {
			count++;
		}
	}

	return count;
}


// A command in the builder, or a closed command in a set. Name is the text of the name, with its plus sign for a command. A name of one
// letter is a basic command.
template<AtText Text, typename... Forms>
struct AtCommandItem {
	static constexpr std::string_view name = Text.View();
	static constexpr bool isCommand = true;
	static constexpr bool isBasic = Text.View().size() == 1;

	using ReadType = typename AtReadValueOf<Forms...>::Type;
	using WriteArguments = typename AtWriteArgsOf<Forms...>::Type;
	using ValueArguments = typename AtValueArgsOf<Forms...>::Type;

	// The forms in declaration order, and the arguments the server reads from the line: the write arguments, or the value arguments of a
	// basic command. A command has at most one of the two.
	using FormTypes = std::tuple<Forms...>;
	using FieldArguments = std::conditional_t<(std::tuple_size_v<WriteArguments> > 0), WriteArguments, ValueArguments>;
	static constexpr std::size_t FieldCount = std::tuple_size_v<FieldArguments>;

	CommandPack<Forms...> forms{};
	AtCommandOptions options{};


	template<typename Form>
	using With = AtCommandItem<Text, Forms..., Form>;


	template<AtFormKind Kind>
	static constexpr std::size_t CountOf() {
		return AtFormCount<Kind, Forms...>();
	}


	template<typename Form>
	constexpr AtCommandItem<Text, Forms..., Form> WithForm(const Form& form) const {
		return AtCommandItem<Text, Forms..., Form>{CommandPackAppend(forms, form), options};
	}


	constexpr AtCommandItem WithOptions(const AtCommandOptions& changed) const {
		return AtCommandItem{forms, changed};
	}
};


// The placeholder of the builder when no command is open. It is never read: every form and option checks that a command is open.
struct AtNoCommand {
	static constexpr bool isCommand = false;
	static constexpr bool isBasic = false;

	AtCommandOptions options{};


	template<typename Form>
	using With = AtNoCommand;


	template<AtFormKind Kind>
	static constexpr std::size_t CountOf() {
		return 0;
	}


	template<typename Form>
	constexpr AtNoCommand WithForm(const Form& form) const {
		static_cast<void>(form);
		return AtNoCommand{};
	}


	constexpr AtNoCommand WithOptions(const AtCommandOptions& changed) const {
		return AtNoCommand{changed};
	}
};


// A URC: its prefix and its arguments. The prefix ends at a colon, a comma or the end of the line.
template<AtText Text, typename... Arguments>
struct AtUrcItem {
	static constexpr std::string_view name = Text.View();

	using ArgumentTypes = std::tuple<Arguments...>;

	CommandPack<Arguments...> arguments{};
};


// The traits that tell the kinds of item apart.
template<typename Item>
inline constexpr bool AtIsCommandItem = false;


template<AtText Text, typename... Forms>
inline constexpr bool AtIsCommandItem<AtCommandItem<Text, Forms...>> = true;


template<typename Item>
inline constexpr bool AtIsUrcItem = false;


template<AtText Text, typename... Arguments>
inline constexpr bool AtIsUrcItem<AtUrcItem<Text, Arguments...>> = true;


// True for a form that carries the arguments the server reads from a line: a write form or a value form.
template<typename Form>
inline constexpr bool AtIsArgumentForm = false;


template<typename... Arguments>
inline constexpr bool AtIsArgumentForm<AtWriteForm<Arguments...>> = true;


template<typename... Arguments>
inline constexpr bool AtIsArgumentForm<AtValueForm<Arguments...>> = true;
