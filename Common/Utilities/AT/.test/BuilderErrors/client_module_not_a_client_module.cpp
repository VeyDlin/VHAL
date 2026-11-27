// expect: a client module is not an AtQueue or AtEcho
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/Modules/AtDataMode.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius");
}>;

using Binding = AtClientBinding<>;

using Client = AtClient<Set, Binding, AtDataModeTag>;

static_assert(sizeof(Client) > 0);
