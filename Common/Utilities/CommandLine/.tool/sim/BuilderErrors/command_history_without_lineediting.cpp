// expect: a module needs a feature that no listed module provides
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandHistory.h>
#include <Utilities/CommandLine/CommandLine.h>


// History recalls lines into the buffer of LineEditing. Without LineEditing listed, nothing provides that buffer.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, History<4, 64>> console(app, out);
	static_cast<void>(console);
}
