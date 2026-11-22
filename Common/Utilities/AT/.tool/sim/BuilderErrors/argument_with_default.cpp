// expect: an AT argument has no default; declare it with Optional<T>
#include <Utilities/AT/AtCommandSet.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<uint8>("level").Default(3));
}>;

static_assert(Set::CommandCount == 1);
