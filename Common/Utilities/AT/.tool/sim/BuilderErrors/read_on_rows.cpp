// expect: AT command's read form is ReadRows: call ReadRows
#include <Utilities/AT/AtClient.h>

struct Access {
	int8 value = 0;
};

constexpr auto AccessRow = AtRow<Access>(AtField<&Access::value>("value"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+SCAN">().ReadRows(AccessRow);
}>;

using Binding = AtClientBinding<>;

void OnScan(Result<int8, AtStatus> result) {
	static_cast<void>(result);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>> client(out);
	client.Read<"+SCAN", &OnScan>();
}
