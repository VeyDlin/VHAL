// expect: a group has no Default() or OnInterrupt()
#include "CommandErrorApp.h"


// A group with its own Default(). The console calls only the top set's handlers, so the builder refuses the group's handler rather
// than accept one that never runs.
using Inner = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("gain", "set the gain", &ErrKey, Argument<uint8>("code"))
		.Default(&ErrEvent);
}>;

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Group<Inner>("motor", "motor control");
}>;

static_assert(Set::GroupCount == 1);
