// expect: AwaitRead needs AtAwait among the client modules
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

void Use(AtClient<Set, Binding, AtQueue<2>>& client) {
	static_cast<void>(client.AwaitRead<"+TEMP">());
}
