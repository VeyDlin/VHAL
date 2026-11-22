// expect: an AT handler does not match a form of its command
#include <Utilities/AT/AtServer.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

struct App {
};

AtStatus ListWrong(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{};
}

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LIST">().Rows(AccessRow);
}>;

using Binding = AtServerBinding<AtOn<"+LIST", &ListWrong>>;

using Server = AtServer<Set, App, Binding>;

static_assert(sizeof(Server) > 0);
