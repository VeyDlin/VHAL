// expect: a form is declared twice for one command
#include <Utilities/AT/AtCommandSet.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CWLAP">().Read<int8>("value").ReadRows(AccessRow);
}>;

static_assert(Set::CommandCount == 1);
