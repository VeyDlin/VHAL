// expect: Prompt() is for a write form, which comes before it
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Prompt().Write(Argument<uint16>("length")).Finals("SEND OK");
}>;

static_assert(sizeof(Set) > 0);
