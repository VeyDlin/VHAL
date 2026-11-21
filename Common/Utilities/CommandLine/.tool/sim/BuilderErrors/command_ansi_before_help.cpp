// expect: Help must be listed before a module that takes characters, keys or events (LineEditing and History are exempt)
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandAnsi.h>
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>


// Ansi takes Ctrl+L, so it must come after Help, which reads every character of the line.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code"));
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, Ansi, Help> console(app, out);
	static_cast<void>(console);
}
