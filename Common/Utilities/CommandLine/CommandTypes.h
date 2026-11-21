#pragma once
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <string_view>


// The types the command line shares between its declarations and its console: the kinds of arguments, the faults an argument can
// report, the events, the call context of a handler, the text value of a string argument, and the reader interface of one argument.

class Print;


// The kinds of argument. A custom argument type reports Custom. Float is a float or double argument (FloatArguments.h), and Secret is a
// string that is echoed as '*' (HiddenInput.h).
enum class CommandArgumentKind : uint8 {
	Integer,
	Boolean,
	Enumeration,
	Text,
	Custom,
	Float,
	Secret
};


// Why an argument's text is refused, or why a command line cannot run. None means the value is read.
enum class CommandFault : uint8 {
	None,
	Invalid,
	OutOfRange,
	TooLong,
	Missing,
	Extra,
	MissingQuote
};


// The events the console raises. Each one goes to its own handler, or to the default handler when it has none.
enum class CommandEventKind : uint8 {
	Command,
	CommandFailed,
	UnknownCommand,
	ArgumentError,
	EmptyLine,
	Word,
	EndOfLine,
	Interrupt,
	TerminalOpened,
	TerminalClosed,
	InputOverflow
};


// The range of an integer, text or secret argument. A 64-bit unsigned maximum above the int64 range is in unsignedMaximum, with
// maximumUnsigned set; maximum then holds the clamp. For a text argument maximum is the capacity.
struct CommandIntegerRange {
	int64 minimum = 0;
	int64 maximum = 0;
	bool maximumUnsigned = false;
	uint64 unsignedMaximum = 0;
};


// The range of a float argument.
struct CommandFloatRange {
	double minimum = 0;
	double maximum = 0;
};


// What the default handler gets. text is the word the event is about (UnknownCommand, Word), and status is the result of a command
// that failed. command is the command the event is about, argument the argument of an ArgumentError, with its kind, fault and
// range. A string that is not about an argument leaves these empty. The range is range.integers for an integer, text or secret
// argument, and range.floats for a float argument, as argumentKind tells. The two share their storage, so an event costs the same
// whether or not the set has a float argument. truncated is set on a Word event whose word is longer than the word buffer, and text
// then holds the first characters of the word.
struct CommandEvent {
	CommandEventKind kind = CommandEventKind::Command;
	std::string_view text;
	ResultStatus status = ResultStatus::ok;
	std::string_view command;
	std::string_view argument;
	CommandArgumentKind argumentKind = CommandArgumentKind::Custom;
	CommandFault fault = CommandFault::None;

	union {
		CommandIntegerRange integers{};
		CommandFloatRange floats;
	} range;

	bool truncated = false;
};


// What a command handler gets as its first parameter: the console's output, and the application as Context. Context is the type
// the handler declares, which the application type must be or derive from.
template<typename Context>
struct CommandCall {
	Print& out;
	Context& app;
};


// The first parameter of a long command's handler (LongCommands). state is the handler's own state, which the console makes before
// the first call and keeps until the command ends. cancelled is true on the one call that Ctrl+C makes, and the step it returns is
// then ignored.
template<typename Context, typename State>
struct CommandLongCall {
	Print& out;
	Context& app;
	State& state;
	bool cancelled;
};


// The tag of CommandContinue. A long handler returns CommandContinue to run again on the next Process pass.
struct CommandContinueTag {
};


inline constexpr CommandContinueTag CommandContinue{};


// What a long command's handler returns: CommandContinue, or a final ResultStatus. A ResultStatus converts to a final step, so the
// handler of an ordinary command can return one too.
struct CommandStep {
	ResultStatus status = ResultStatus::ok;
	bool continues = false;


	constexpr CommandStep(ResultStatus result) : status(result), continues(false) {
	}


	constexpr CommandStep(CommandContinueTag) : status(ResultStatus::ok), continues(true) {
	}
};


// The call the console makes on a command: Start makes the state and runs the first step, Continue runs the next step, and Cancel is
// the one call of Ctrl+C.
enum class CommandStepMode : uint8 {
	Start,
	Continue,
	Cancel
};


// A string argument's value, kept as its characters. Capacity is the most characters it can hold.
template<std::size_t Capacity>
struct String {
	static_assert(Capacity > 0, "a string argument needs a capacity of at least 1");

	std::array<char, Capacity> characters{};
	std::size_t length = 0;


	constexpr std::string_view View() const {
		return std::string_view(characters.data(), length);
	}
};


// The reader of one argument. The console creates it in storage it owns, so a slot has no destructor and the parser of a slot must
// be trivially destructible.
//
// Push takes one character of the argument's text and returns false when the character is refused. Once a character is refused,
// every later character is refused as well, until Pop removes the refused ones again. So Pop always undoes the last character the
// console passed in, and the reader never has to remember which characters it kept.
//
// Finish writes the value to destination when it returns None. destination is the argument's place in the command's value area.
//
// Quoting says whether the text read so far is inside a quoted string, where a space is part of the argument and does not end it.
// Only a string argument can be quoted, so the others keep this default.
class CommandSlot {
public:
	virtual bool Push(char character) = 0;

	virtual void Pop() = 0;

	virtual CommandFault Finish(std::byte* destination) const = 0;


	virtual bool Quoting() const {
		return false;
	}
};
