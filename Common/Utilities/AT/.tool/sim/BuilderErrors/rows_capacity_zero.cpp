// expect: AtRows needs a capacity of at least 1
#include <Utilities/AT/AtClient.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LIST">().Rows(AccessRow);
}>;

using Binding = AtClientBinding<>;

using Client = AtClient<Set, Binding, AtQueue<2>, AtRows<0>>;

static_assert(sizeof(Client) > 0);
