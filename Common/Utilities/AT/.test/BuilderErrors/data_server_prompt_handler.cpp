// expect: a prompt command has no server handler
#include <Utilities/AT/AtServer.h>

struct App {
};

AtStatus SendData(AtCall<App> call, uint16 length) {
	static_cast<void>(call);
	static_cast<void>(length);
	return AtStatus{};
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK");
}>;

using Server = AtServer<Set, App, AtServerBinding<AtOn<"+CIPSEND", &SendData>>>;

static_assert(sizeof(Server) > 0);
