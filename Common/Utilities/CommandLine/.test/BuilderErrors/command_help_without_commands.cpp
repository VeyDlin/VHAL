// expect: Help needs a command or a group in the set
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>


// A set with only a default handler has no commands to describe, so a listed Help is an error. Process forces the line end, where
// Help is used.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, Help> console(app, out);
	console.Process();
}
