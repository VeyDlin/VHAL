// expect: an AT client queue needs a depth of at least 1
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

using Client = AtClient<Set, Binding, AtQueue<0>>;

static_assert(sizeof(Client) > 0);
