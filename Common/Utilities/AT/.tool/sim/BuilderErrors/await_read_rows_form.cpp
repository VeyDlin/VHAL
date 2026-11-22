// expect: call AwaitReadRows
#include <Utilities/AT/AtClient.h>

struct Net {
	int8 rssi = 0;
};

constexpr auto NetRow = AtRow<Net>(AtField<&Net::rssi>("rssi"));

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+SCAN">().ReadRows(NetRow);
}>;

using Binding = AtClientBinding<>;

void Use(AtClient<Set, Binding, AtQueue<2>, AtRows<4>, AtAwait>& client) {
	static_cast<void>(client.AwaitRead<"+SCAN">());
}
