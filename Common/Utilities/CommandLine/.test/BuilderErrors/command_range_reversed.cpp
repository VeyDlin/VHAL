// expect: a range has its minimum above its maximum
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrKey, Argument<uint8>("code").Range(200, 100));
}>;

static_assert(Set::CommandCount == 1);
