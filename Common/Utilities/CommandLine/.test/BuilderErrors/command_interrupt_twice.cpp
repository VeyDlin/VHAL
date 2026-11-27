// expect: OnInterrupt() is given twice
#include "CommandErrorApp.h"

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.OnInterrupt(&ErrStop)
		.OnInterrupt(&ErrStop);
}>;

static_assert(Set::HasInterrupt);
