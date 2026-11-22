// expect: AT command name is not in the description
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

void OnTemp(Result<int16, AtStatus> result) {
	static_cast<void>(result);
}

void Use(AtClient<Set, Binding>& client) {
	client.Read<"+TMEP", &OnTemp>();
}
