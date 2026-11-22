// expect: AtRows is listed twice in the client modules
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

using Client = AtClient<Set, Binding, AtRows<1>, AtRows<2>>;

static_assert(sizeof(Client) > 0);
