// expect: a URC prefix is a prefix of a reply prefix, or the reverse
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+ABC">().Run().Reply("+XYZ:")
		.Urc<"+XYZ">();
}>;

static_assert(Set::CommandCount == 1);
