// expect: a command form has no server handler, or has two for one form
#include <Utilities/AT/AtServer.h>

struct App {
};

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtServerBinding<>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
