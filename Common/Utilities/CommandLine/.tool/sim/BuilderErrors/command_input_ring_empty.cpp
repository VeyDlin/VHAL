// expect: a command line input ring needs at least one byte
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// A ring with no bytes cannot hold a press's place, so InputRing needs a size above zero.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, InputRing<0>> console(app, out);
	static_cast<void>(console);
}
