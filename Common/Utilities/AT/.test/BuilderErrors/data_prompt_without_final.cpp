// expect: a prompt command needs a declared final, such as SEND OK, to end after its data
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt();
}>;

static_assert(sizeof(Set) > 0);
