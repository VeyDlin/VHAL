// expect: AT command has no run form
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

void OnStatus(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding>& client) {
	client.Run<"+TEMP", &OnStatus>();
}
