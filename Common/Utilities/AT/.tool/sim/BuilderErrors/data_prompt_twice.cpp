// expect: Prompt() is given twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Prompt().Finals("SEND OK");
}>;

static_assert(sizeof(Set) > 0);
