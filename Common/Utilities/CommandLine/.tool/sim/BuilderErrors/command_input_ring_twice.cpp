// expect: a console lists at most one InputRing
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// Two input rings would not say which size the console has, so the console refuses the second.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, InputRing<8>, InputRing<16>> console(app, out);
	static_cast<void>(console);
}
