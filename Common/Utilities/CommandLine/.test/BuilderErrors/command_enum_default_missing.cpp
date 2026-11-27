// expect: the default is not one of the enum values
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("mode", "pick a mode", &ErrSetMode, Argument<ErrMode>("mode").Values({{"fast", ErrMode::Fast}}).Default(ErrMode::Slow));
}>;

static_assert(Set::CommandCount == 1);
