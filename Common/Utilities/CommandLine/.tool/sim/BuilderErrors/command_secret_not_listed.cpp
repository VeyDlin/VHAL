// expect: a Secret argument needs the HiddenInput module, listed in the console
#include "CommandErrorApp.h"
#include <Utilities/CommandLine/CommandHiddenInput.h>
#include <Utilities/CommandLine/CommandLine.h>


// The header is included, but the module is not listed: the secret would be echoed in clear, so the console is refused.
ResultStatus ErrLogin(CommandCall<ErrApp> call, Secret<8> password) {
	static_cast<void>(call);
	static_cast<void>(password);
	return ResultStatus::ok;
}


using Set = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("login", "log in", &ErrLogin, Argument<Secret<8>>("password"));
}>;


void Use(ErrApp& app, Print& out) {
	CommandLine<Set, ErrApp> console(app, out);
	static_cast<void>(console);
}
