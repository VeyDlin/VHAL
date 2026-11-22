#pragma once
#include "AtArgument.h"
#include "AtError.h"
#include "Modules/AtDataMode.h"
#include <Utilities/CommandLine/CommandPack.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>


// The forms of a command and the items of a set. A form is a type that names its kind, and its values (a read name, an argument list, a
// row declaration) are plain values, so the set keeps both the types and the declared values. A command item is a name, its forms and its
// options.
//
// A reply form is a read form or a run form whose info lines carry a reply: one value (AtReadForm, AtRunValueForm), one record of fields
// (AtReadRecordForm, AtRunRecordForm), or a list of rows (AtReadRowsForm, AtRunRowsForm). A command has at most one read form and at most
// one run form, so its reply shape is known from its type.


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


// The shape of the info lines of a reply form: one value, one record (one info line with fields), or rows (one info line for each row).
enum class AtReplyShape : uint8 {
	value,
	record,
	rows
};


// AT+X=?
struct AtTestForm {
	static constexpr AtFormKind kind = AtFormKind::test;
};


// AT+X?, with one value of type T, which the info line carries.
template<typename T>
struct AtReadForm {
	using Type = T;
	static constexpr AtFormKind kind = AtFormKind::read;
	static constexpr AtReplyShape shape = AtReplyShape::value;
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


// AT+X, with one value of type T in its info line, such as +CIFSR:STAIP,"...".
template<typename T>
struct AtRunValueForm {
	using Type = T;
	static constexpr AtFormKind kind = AtFormKind::run;
	static constexpr AtReplyShape shape = AtReplyShape::value;
	std::string_view name{};
};


// AT+X?, with one info line whose fields are the fields of the row declaration RowDecl (AtRow in AtRow.h).
template<typename RowDecl>
struct AtReadRecordForm {
	using Decl = RowDecl;
	using Type = typename RowDecl::RowType;
	static constexpr AtFormKind kind = AtFormKind::read;
	static constexpr AtReplyShape shape = AtReplyShape::record;
	RowDecl decl{};
};


// AT+X, with one info line whose fields are the fields of the row declaration RowDecl.
template<typename RowDecl>
struct AtRunRecordForm {
	using Decl = RowDecl;
	using Type = typename RowDecl::RowType;
	static constexpr AtFormKind kind = AtFormKind::run;
	static constexpr AtReplyShape shape = AtReplyShape::record;
	RowDecl decl{};
};


// AT+X?, with one info line for each row of the declaration RowDecl.
template<typename RowDecl>
struct AtReadRowsForm {
	using Decl = RowDecl;
	using Type = typename RowDecl::RowType;
	static constexpr AtFormKind kind = AtFormKind::read;
	static constexpr AtReplyShape shape = AtReplyShape::rows;
	RowDecl decl{};
};


// AT+X, with one info line for each row of the declaration RowDecl.
template<typename RowDecl>
struct AtRunRowsForm {
	using Decl = RowDecl;
	using Type = typename RowDecl::RowType;
	static constexpr AtFormKind kind = AtFormKind::run;
	static constexpr AtReplyShape shape = AtReplyShape::rows;
	RowDecl decl{};
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


// True for a form whose info lines carry a reply: a read or run form with a value, a record or rows.
template<typename Form>
inline constexpr bool AtIsInfoForm = false;


template<typename T>
inline constexpr bool AtIsInfoForm<AtReadForm<T>> = true;


template<typename T>
inline constexpr bool AtIsInfoForm<AtRunValueForm<T>> = true;


template<typename RowDecl>
inline constexpr bool AtIsInfoForm<AtReadRecordForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsInfoForm<AtRunRecordForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsInfoForm<AtReadRowsForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsInfoForm<AtRunRowsForm<RowDecl>> = true;


// True for a form whose info lines are fields of a row: a record or rows. Such a form has a row declaration with its Open and Close.
template<typename Form>
inline constexpr bool AtIsRowForm = false;


template<typename RowDecl>
inline constexpr bool AtIsRowForm<AtReadRecordForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsRowForm<AtRunRecordForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsRowForm<AtReadRowsForm<RowDecl>> = true;


template<typename RowDecl>
inline constexpr bool AtIsRowForm<AtRunRowsForm<RowDecl>> = true;


// The info form of a kind in a list of forms: the first form of that kind with an info line, or void when there is none.
template<AtFormKind Kind, typename... Forms>
struct AtInfoFormOf {
	using Type = void;
};


template<AtFormKind Kind, typename Head, typename... Tail>
struct AtInfoFormOf<Kind, Head, Tail...> {
	using Type = std::conditional_t<(Head::kind == Kind && AtIsInfoForm<Head>), Head, typename AtInfoFormOf<Kind, Tail...>::Type>;
};


// The type a reply carries: the value type, or the row type of a record or rows form. Void for no form.
template<typename Form>
struct AtInfoTypeOf {
	using Type = typename Form::Type;
};


template<>
struct AtInfoTypeOf<void> {
	using Type = void;
};


// The shape of a reply form. A command with no reply form has the value shape, which no call of that form reads.
template<typename Form>
struct AtShapeOf {
	static constexpr AtReplyShape value = Form::shape;
};


template<>
struct AtShapeOf<void> {
	static constexpr AtReplyShape value = AtReplyShape::value;
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


// A command in the builder, or a closed command in a set. Name is the text of the name, with its plus sign for a command. A name of one
// letter is a basic command.
template<TextLiteral Text, typename... Forms>
struct AtCommandItem {
	static constexpr std::string_view name = Text.View();
	static constexpr bool isCommand = true;
	static constexpr bool isBasic = Text.View().size() == 1;

	// The reply forms: the read form's info, and the run form's info (void when a form is missing). A run form with no info line is
	// AtRunForm, and it is in hasPlainRun.
	using ReadInfo = typename AtInfoFormOf<AtFormKind::read, Forms...>::Type;
	using RunInfo = typename AtInfoFormOf<AtFormKind::run, Forms...>::Type;
	using ReadType = typename AtInfoTypeOf<ReadInfo>::Type;
	using RunType = typename AtInfoTypeOf<RunInfo>::Type;
	static constexpr AtReplyShape readShape = AtShapeOf<ReadInfo>::value;
	static constexpr AtReplyShape runShape = AtShapeOf<RunInfo>::value;
	static constexpr bool hasPlainRun = (std::is_same_v<AtRunForm, Forms> || ...);

	using WriteArguments = typename AtWriteArgsOf<Forms...>::Type;
	using ValueArguments = typename AtValueArgsOf<Forms...>::Type;

	// The forms in declaration order, and the arguments the server reads from the line: the write arguments, or the value arguments of a
	// basic command. A command has at most one of the two.
	using FormTypes = std::tuple<Forms...>;
	using FieldArguments = std::conditional_t<(std::tuple_size_v<WriteArguments> > 0), WriteArguments, ValueArguments>;
	static constexpr std::size_t FieldCount = std::tuple_size_v<FieldArguments>;

	CommandPack<Forms...> forms{};
	AtCommandOptions options{};
	bool prompt = false;
	std::string_view failureCodePrefix{};


	template<typename Form>
	using With = AtCommandItem<Text, Forms..., Form>;


	template<AtFormKind Kind>
	static constexpr std::size_t CountOf() {
		return AtFormCount<Kind, Forms...>();
	}


	template<typename Form>
	constexpr AtCommandItem<Text, Forms..., Form> WithForm(const Form& form) const {
		return AtCommandItem<Text, Forms..., Form>{CommandPackAppend(forms, form), options, prompt, failureCodePrefix};
	}


	constexpr AtCommandItem WithOptions(const AtCommandOptions& changed) const {
		return AtCommandItem{forms, changed, prompt, failureCodePrefix};
	}


	constexpr AtCommandItem WithPrompt() const {
		return AtCommandItem{forms, options, true, failureCodePrefix};
	}


	constexpr AtCommandItem WithFailureCode(std::string_view prefix) const {
		return AtCommandItem{forms, options, prompt, prefix};
	}
};


// The placeholder of the builder when no command is open. It is never read: every form and option checks that a command is open.
struct AtNoCommand {
	static constexpr bool isCommand = false;
	static constexpr bool isBasic = false;

	AtCommandOptions options{};
	bool prompt = false;
	std::string_view failureCodePrefix{};


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
		return AtNoCommand{changed, prompt, failureCodePrefix};
	}


	constexpr AtNoCommand WithPrompt() const {
		return AtNoCommand{options, true, failureCodePrefix};
	}


	constexpr AtNoCommand WithFailureCode(std::string_view prefix) const {
		return AtNoCommand{options, prompt, prefix};
	}
};


// A URC: its prefix, its arguments, and its Data(). The arguments are the ones before a Data() last argument, so the length is the last of
// them, and Data is AtDataLength<T> for a URC with Data() (T is the length's type) or AtNoData. The prefix ends at a colon, a comma or the
// end of the line.
template<TextLiteral Text, typename Data, typename... Arguments>
struct AtUrcItem {
	static constexpr std::string_view name = Text.View();
	static constexpr bool hasData = AtIsDataLength<Data>;

	using DataType = Data;
	using ArgumentTypes = std::tuple<Arguments...>;

	CommandPack<Arguments...> arguments{};
};


// The traits that tell the kinds of item apart.
template<typename Item>
inline constexpr bool AtIsCommandItem = false;


template<TextLiteral Text, typename... Forms>
inline constexpr bool AtIsCommandItem<AtCommandItem<Text, Forms...>> = true;


template<typename Item>
inline constexpr bool AtIsUrcItem = false;


template<TextLiteral Text, typename Data, typename... Arguments>
inline constexpr bool AtIsUrcItem<AtUrcItem<Text, Data, Arguments...>> = true;


// True for a form that carries the arguments the server reads from a line: a write form or a value form.
template<typename Form>
inline constexpr bool AtIsArgumentForm = false;


template<typename... Arguments>
inline constexpr bool AtIsArgumentForm<AtWriteForm<Arguments...>> = true;


template<typename... Arguments>
inline constexpr bool AtIsArgumentForm<AtValueForm<Arguments...>> = true;
