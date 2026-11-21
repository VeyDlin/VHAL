// expect: the context of a handler is not the application type or one of its bases
#include "CommandErrorApp.h"

struct OtherApp {
	int value = 0;
};

inline ResultStatus OtherKey(CommandCall<OtherApp> call, uint8 code) {
	call.app.value = code;
	return ResultStatus::ok;
}

using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &OtherKey, Argument<uint8>("code"));
}>;

// ErrApp is not OtherApp, nor derived from it, so the binding check must fail.
static_assert(Set::CommandCount == 1);
static_assert(sizeof(Set) > 0);

int main() {
	Set::CheckContext<ErrApp>();
	return 0;
}
