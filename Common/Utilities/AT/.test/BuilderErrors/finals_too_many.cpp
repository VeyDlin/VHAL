// expect: a command has too many final lines
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIPSEND">().Run()
		.Finals("A")
		.Finals("B")
		.Finals("C")
		.Finals("D")
		.Finals("E");
}>;

static_assert(Set::CommandCount == 1);
