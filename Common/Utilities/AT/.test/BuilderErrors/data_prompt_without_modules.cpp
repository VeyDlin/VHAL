// expect: a prompt command needs AtDataMode among the modules of the set
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK");
}>;

static_assert(sizeof(Set) > 0);
