// expect: ReadRows needs the AtRows module in the client modules
#include <Utilities/AT/AtClient.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LIST">().ReadRows(AccessRow);
}>;

using Binding = AtClientBinding<>;

void OnList(const Result<AtRowList<Access, 1>, AtStatus>& result) {
	static_cast<void>(result);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>> client(out);
	client.ReadRows<"+LIST", &OnList>();
}
