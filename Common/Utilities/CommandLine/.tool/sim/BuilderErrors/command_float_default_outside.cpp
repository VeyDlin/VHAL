// expect: the default is outside the range of the argument
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
		.Command("speed", "set the speed", &ErrSpeed, Argument<float>("rpm").Range(0.0f, 1.0f).Default(2.0f));
}>;

static_assert(Set::EntryCount == 1);
