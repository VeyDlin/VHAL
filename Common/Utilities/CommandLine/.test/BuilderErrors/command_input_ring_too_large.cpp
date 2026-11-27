// expect: a command line input ring holds at most 2^30 bytes
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// The ring compares positions by their signed difference, which is exact only below 2^31. A ring of 2^31 bytes can be full with a
// refused Ctrl+C at its head, and then that difference is -2^31, so the largest power of two that is safe is 2^30.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, InputRing<2147483648>> console(app, out);
	static_cast<void>(console);
}
