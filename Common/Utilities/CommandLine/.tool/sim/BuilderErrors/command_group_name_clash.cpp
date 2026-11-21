// expect: duplicate names at one level
#include "CommandErrorApp.h"

using Inner = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code"));
}>;

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("inner", "a command", &ErrKey, Argument<uint8>("code"))
		.Group<Inner>("inner", "a group with the same name");
}>;

static_assert(Set::EntryCount == 2);
