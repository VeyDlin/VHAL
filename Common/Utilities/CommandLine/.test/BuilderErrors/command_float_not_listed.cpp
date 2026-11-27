// expect: a float argument needs the FloatArguments module, listed in the console
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandFloatArguments.h>
#include <Utilities/CommandLine/CommandLine.h>


// The header is included, but the module is not listed, so the console is refused.
ResultStatus ErrSpeed(CommandCall<ErrApp> call, float rpm) {
	static_cast<void>(call);
	static_cast<void>(rpm);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &ErrSpeed, Argument<float>("rpm"));
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp> console(app, out);
	static_cast<void>(console);
}
