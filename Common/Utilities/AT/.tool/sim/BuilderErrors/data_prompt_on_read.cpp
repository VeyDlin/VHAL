// expect: Prompt() is for a write form, which comes before it
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Read<int16>("celsius").Prompt().Finals("SEND OK");
}>;

static_assert(sizeof(Set) > 0);
