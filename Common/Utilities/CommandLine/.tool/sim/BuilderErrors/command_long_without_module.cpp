// expect: a long command needs the LongCommands module, listed in the console
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// A long command returns a CommandStep. Without LongCommands listed, the console has no place to keep its state, so it is refused.
struct ErrLongState {
	int done = 0;
};


CommandStep ErrLong(CommandLongCall<ErrApp, ErrLongState> call) {
	static_cast<void>(call);
	return CommandContinue;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("long", "a long command", &ErrLong);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp> console(app, out);
	static_cast<void>(console);
}
