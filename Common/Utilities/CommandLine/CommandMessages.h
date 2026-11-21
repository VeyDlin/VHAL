#pragma once
#include "CommandStatusText.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>
#include <cstddef>


// The standard default handler: the text the console prints for an event when the set has no Default() of its own. The console prints
// the prompt after each line, so EmptyLine, Word, EndOfLine, TerminalClosed and Command print nothing here. Only CommandLine.h calls
// this, and only when the set has no Default(), so CommandStatusNames is linked only then.


inline void CommandWriteRange(Print& out, const CommandEvent& event) {
	out.Write(event.minimum);
	out.Write("..");

	if (event.maximumUnsigned) {
		out.Write(event.unsignedMaximum);
	} else {
		out.Write(event.maximum);
	}
}


// The reason an argument is refused, after the argument's name.
inline void CommandWriteProblem(Print& out, const CommandEvent& event) {
	switch (event.fault) {
		case CommandFault::Missing:
			out.Write(event.argument);
			out.Write(" is missing");
			return;
		case CommandFault::Extra:
			out.Write("too many arguments");
			return;
		case CommandFault::MissingQuote:
			out.Write("missing closing quote");
			return;
		case CommandFault::OutOfRange:
			out.Write(event.argument);
			out.Write(" must be ");
			CommandWriteRange(out, event);
			return;
		case CommandFault::TooLong:
			out.Write(event.argument);
			out.Write(" must be at most ");
			out.Write(event.maximum);
			out.Write(" characters");
			return;
		default:
			break;
	}

	out.Write(event.argument);

	switch (event.argumentKind) {
		case CommandArgumentKind::Integer:
			out.Write(" must be a number");
			return;
		case CommandArgumentKind::Boolean:
			out.Write(" must be on, off, true, false, 1 or 0");
			return;
		case CommandArgumentKind::Enumeration:
			out.Write(" must be a listed value");
			return;
		default:
			out.Write(" is not valid");
			return;
	}
}


inline void CommandWriteEvent(Print& out, const CommandEvent& event) {
	switch (event.kind) {
		case CommandEventKind::CommandFailed:
			out.Write(event.command);
			out.Write(": ");
			out.Write(CommandStatusNames[static_cast<std::size_t>(event.status)]);
			out.Line();
			return;
		case CommandEventKind::UnknownCommand:
			out.Write("unknown command '");
			out.Write(event.text);
			out.Write("', try help");
			out.Line();
			return;
		case CommandEventKind::ArgumentError:
			out.Write(event.command);
			out.Write(": ");
			CommandWriteProblem(out, event);
			out.Line();
			return;
		case CommandEventKind::InputOverflow:
			out.Write("input lost");
			out.Line();
			return;
		case CommandEventKind::Interrupt:
			out.Write("^C");
			out.Line();
			return;
		case CommandEventKind::TerminalOpened:
			out.Write("VHAL command line");
			out.Line();
			return;
		default:
			return;
	}
}
