// expect: Rows is for a command whose run form is Rows(row)
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIFSR">().Run<int8>("value");
}>;

using Binding = AtClientBinding<>;

void OnList(const Result<AtRowList<int8, 1>, AtStatus>& result) {
	static_cast<void>(result);
}

void Use(Print& out) {
	AtClient<Set, Binding, AtQueue<2>, AtRows<1>> client(out);
	client.Rows<"+CIFSR", &OnList>();
}
