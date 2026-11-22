// expect: an AT handler does not match a form of its command
#include <Utilities/AT/AtServer.h>

struct App {
};

AtStatus Temp(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{};
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtServerBinding<AtOn<"+TEMP", &Temp>>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
