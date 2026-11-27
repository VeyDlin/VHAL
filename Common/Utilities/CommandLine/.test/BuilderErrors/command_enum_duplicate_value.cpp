// expect: duplicate enum names
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("mode", "pick a mode", &ErrSetMode, Argument<ErrMode>("mode").Values({{"fast", ErrMode::Fast}, {"fast", ErrMode::Slow}}));
}>;

static_assert(Set::CommandCount == 1);
