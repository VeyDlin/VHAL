// expect: a command line input ring needs a power-of-two size
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandLine.h>


// The ring's index is its counter modulo the size. A 32-bit counter wraps at 2^32, which only a power-of-two size divides, so any other
// size would store a byte in the wrong slot after the wrap.
using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent);
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp, InputRing<50>> console(app, out);
	static_cast<void>(console);
}
