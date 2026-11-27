// expect: a default handler is void(CommandCall<Ctx>, const CommandEvent&)
#include "CommandErrorApp.h"

inline ResultStatus ErrBadDefault(CommandCall<ErrApp> call, const CommandEvent& event) {
	static_cast<void>(call);
	static_cast<void>(event);
	return ResultStatus::ok;
}

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&ErrBadDefault);
}>;

static_assert(Set::HasDefault);
