// expect: a URC prefix is declared twice
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Urc<"WIFI GOT IP">()
		.Urc<"WIFI GOT IP">();
}>;

static_assert(Set::UrcCount == 2);
