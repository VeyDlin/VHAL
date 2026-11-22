// expect: a basic command has no test, read or write form
#include <Utilities/AT/AtCommandSet.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Basic<"X">().Read(AccessRow);
}>;

static_assert(Set::CommandCount == 1);
