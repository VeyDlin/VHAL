// expect: a URC prefix is a prefix of a final line, so the URC would take the final line
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+RUN">().Run()
		.Urc<"ER">();
}>;

static_assert(Set::CommandCount == 1);
