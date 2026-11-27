// expect: an interrupt handler is void(Ctx&)
#include "CommandErrorApp.h"

inline ResultStatus ErrBadStop(ErrApp& app) {
	app.value = 0;
	return ResultStatus::ok;
}

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.OnInterrupt(&ErrBadStop);
}>;

static_assert(Set::HasInterrupt);
