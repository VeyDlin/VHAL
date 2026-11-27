// expect: a command form has no server handler, or has two for one form
#include <Utilities/AT/AtServer.h>

struct App {
};

Result<int16, AtStatus> Temp(AtCall<App> call) {
	static_cast<void>(call);
	return static_cast<int16>(1);
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtServerBinding<AtOn<"+TEMP", &Temp>, AtOn<"+TEMP", &Temp>>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
