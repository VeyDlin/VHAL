// expect: a Run handler with a reply is void(Result<T, AtStatus>)
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIFSR">().Run<int8>("value");
}>;

using Binding = AtClientBinding<>;

void OnRun(AtStatus status) {
	static_cast<void>(status);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>> client(out);
	client.Run<"+CIFSR", &OnRun>();
}
