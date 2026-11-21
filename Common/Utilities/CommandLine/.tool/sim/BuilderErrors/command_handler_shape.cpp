// expect: a command handler returns ResultStatus and takes CommandCall<Ctx> first
#include "CommandErrorApp.h"

inline void ErrVoidKey(CommandCall<ErrApp> call, uint8 code) {
	call.app.value = code;
}

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ErrVoidKey, Argument<uint8>("code"));
}>;

static_assert(Set::CommandCount == 1);
