// expect: AT command name is not in the description
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

static_assert(Set::Index<"+TMEP">() == 0);
