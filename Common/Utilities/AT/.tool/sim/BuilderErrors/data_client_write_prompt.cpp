// expect: a prompt command takes SendData, not Write
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK");
}>;

using Binding = AtClientBinding<>;

void OnSent(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding, AtQueue<2>>& client) {
	client.Write<"+CIPSEND", &OnSent>(uint16{5});
}
