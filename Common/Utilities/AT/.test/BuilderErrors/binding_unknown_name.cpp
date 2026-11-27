// expect: AT command name is not in the description
#include <Utilities/AT/AtServer.h>

struct App {
};

AtStatus Run(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{};
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Run();
}>;

using Binding = AtServerBinding<AtOn<"+TMEP", &Run>>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
