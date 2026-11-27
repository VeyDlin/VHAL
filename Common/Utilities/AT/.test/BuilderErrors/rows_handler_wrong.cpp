// expect: a Rows handler is void(const Result<AtRowList<Row, N>, AtStatus>&)
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

void OnList(AtStatus status) {
	static_cast<void>(status);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>, AtRows<2>> client(out);
	client.Rows<"+LIST", &OnList>();
}
