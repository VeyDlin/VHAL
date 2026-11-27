// expect: a float range bound is not finite
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandFloatArguments.h>
#include <Utilities/CommandLine/CommandSet.h>
#include <limits>


ResultStatus ErrSpeed(CommandCall<ErrApp> call, float rpm) {
	static_cast<void>(call);
	static_cast<void>(rpm);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &ErrSpeed, Argument<float>("rpm").Range(-1.0f, std::numeric_limits<float>::infinity()));
}>;

static_assert(Set::EntryCount == 1);
