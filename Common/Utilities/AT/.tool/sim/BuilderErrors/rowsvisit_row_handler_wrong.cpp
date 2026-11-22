// expect: a row handler is void(const Row&)
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

void OnRow(int8 value) {
	static_cast<void>(value);
}

void OnDone(AtStatus status) {
	static_cast<void>(status);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>> client(out);
	client.RowsVisit<"+LIST", &OnRow, &OnDone>();
}
