// expect: a float argument needs CommandFloatArguments.h, and the FloatArguments module listed in the console
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandSet.h>


// A float argument needs its header, which says how a float is read. Without it the declaration is refused with this message.
ResultStatus ErrSpeed(CommandCall<ErrApp> call, float rpm) {
	static_cast<void>(call);
	static_cast<void>(rpm);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &ErrSpeed, Argument<float>("rpm"));
}>;

static_assert(Set::EntryCount == 1);
