// expect: Help must be listed before a module that takes characters
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandCompletion.h>
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>


// Completion takes the Tab character, so Help must come before it. Listed after, Help would miss the characters Completion takes.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code"));
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, Completion, Help> console(app, out);
	static_cast<void>(console);
}
