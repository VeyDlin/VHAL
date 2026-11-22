// expect: an AT handler does not match a form of its command
#include <Utilities/AT/AtServer.h>

struct App {
};

AtStatus Led(AtCall<App> call, int8 on) {
	static_cast<void>(call);
	static_cast<void>(on);
	return AtStatus{};
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<bool>("on"));
}>;

using Binding = AtServerBinding<AtOn<"+LED", &Led>>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
