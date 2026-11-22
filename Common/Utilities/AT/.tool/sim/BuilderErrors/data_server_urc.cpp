// expect: a server URC with Data() is not in v1
#include <Utilities/AT/AtServer.h>

struct App {
};

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;

using Server = AtServer<Set, App, AtServerBinding<>>;

void Queue(Server& server) {
	server.Urc<"+IPD">(uint16{1});
}
