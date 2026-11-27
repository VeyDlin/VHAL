// expect: a long command's state must be trivially destructible
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLongCommands.h>
#include <string>


// The state of a long command is kept in the console and is not destroyed, so it must not own memory.
CommandStep ErrLongText(CommandLongCall<ErrApp, std::string> call) {
	static_cast<void>(call);
	return CommandContinue;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("long", "a long command", &ErrLongText);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, LongCommands> console(app, out);
	console.Process();
}
