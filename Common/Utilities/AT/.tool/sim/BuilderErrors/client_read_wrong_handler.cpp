// expect: a Read handler is void(Result<T, AtStatus>), with T the type of the read form
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

void OnTemp(int16 value) {
	static_cast<void>(value);
}

void Use(AtClient<Set, Binding>& client) {
	client.Read<"+TEMP", &OnTemp>();
}
