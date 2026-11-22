// expect: a URC is given values that do not match its arguments
#include <Utilities/AT/AtServer.h>

struct ServerApp {
};

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CMD">().Run()
		.Urc<"+IPD">(Argument<uint16>("length"));
}>;

AtStatus Cmd(AtCall<ServerApp> call) {
	static_cast<void>(call);
	return AtStatus{};
}

using Binding = AtServerBinding<AtOn<"+CMD", &Cmd>>;

using Server = AtServer<Set, ServerApp, Binding>;

void Send(Server& server) {
	server.Urc<"+IPD">(std::string_view("4464"));
}
