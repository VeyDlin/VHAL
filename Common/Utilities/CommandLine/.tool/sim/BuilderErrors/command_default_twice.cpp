// expect: Default() is given twice
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrEvent)
		.Default(&ErrEvent);
}>;

static_assert(Set::HasDefault);
