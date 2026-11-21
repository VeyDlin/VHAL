// expect: a module needs a feature that no listed module provides
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// A feature no module in this snippet provides, and a module that needs it.
struct NeededFeature {
};


struct NeedingModule {
	using Needs = NeededFeature;
};


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, NeedingModule> console(app, out);
	static_cast<void>(console);
}
