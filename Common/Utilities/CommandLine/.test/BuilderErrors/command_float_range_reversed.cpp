// expect: a range has its minimum above its maximum
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandFloatArguments.h>
#include <Utilities/CommandLine/CommandSet.h>


ResultStatus ErrSpeed(CommandCall<ErrApp> call, float rpm) {
	static_cast<void>(call);
	static_cast<void>(rpm);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &ErrSpeed, Argument<float>("rpm").Range(2.0f, 1.0f));
}>;

static_assert(Set::EntryCount == 1);
