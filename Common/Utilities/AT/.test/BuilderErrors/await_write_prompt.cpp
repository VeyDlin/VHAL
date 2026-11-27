// expect: a prompt command takes AwaitSendData, not AwaitWrite
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK");
}>;

using Binding = AtClientBinding<>;

void Use(AtClient<Set, Binding, AtQueue<2>, AtAwait>& client) {
	static_cast<void>(client.AwaitWrite<"+CIPSEND">(uint16{5}));
}
