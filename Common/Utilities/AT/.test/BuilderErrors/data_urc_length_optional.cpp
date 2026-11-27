// expect: the length before Data() must be a required uint8, uint16 or uint32 argument
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Urc<"+IPD">(Optional<uint16>("length"), Data());
}>;

static_assert(sizeof(Set) > 0);
