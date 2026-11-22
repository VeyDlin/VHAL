// expect: a command name is declared twice in one set
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+TEMP">().Read<int16>("kelvin");
}>;

static_assert(Set::CommandCount == 2);
