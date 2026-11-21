// expect: a group has no help text
#include "CommandErrorApp.h"

using Inner = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code"));
}>;

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Group<Inner>("inner", "");
}>;

static_assert(Set::GroupCount == 1);
