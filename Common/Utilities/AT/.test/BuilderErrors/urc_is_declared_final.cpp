// expect: a URC prefix is a prefix of a final line, so the URC would take the final line
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+SEND">().Run()
		.Finals("SEND OK")
		.Urc<"SEND">();
}>;

static_assert(Set::CommandCount == 1);
