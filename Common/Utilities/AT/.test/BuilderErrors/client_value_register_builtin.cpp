// expect: Value is for the echo, verbose or quiet builtin, such as E
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Registers());
}>;

using Binding = AtClientBinding<>;

void OnStatus(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding>& client) {
	client.Value<"S", &OnStatus>(uint8{3});
}
