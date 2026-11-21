// expect: an enum value name is longer than 32 characters
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("mode", "pick a mode", &ErrSetMode, Argument<ErrMode>("mode").Values({{"this_name_is_longer_than_thirty_two", ErrMode::Fast}}));
}>;

static_assert(Set::CommandCount == 1);
