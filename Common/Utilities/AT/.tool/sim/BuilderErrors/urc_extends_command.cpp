// expect: a URC prefix is a prefix of a command name, or the reverse
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIP">().Run()
		.Urc<"+CIPX">();
}>;

static_assert(Set::CommandCount == 1);
