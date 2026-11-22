// expect: SendData needs AtDataMode among the modules of the set
#include <Utilities/AT/AtClient.h>
#include <span>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Finals("SEND OK");
}>;

using Binding = AtClientBinding<>;

void OnSent(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding, AtQueue<2>>& client) {
	client.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>());
}
