// expect: SendData is given values that do not match the arguments of its write form
#include <Utilities/AT/AtClient.h>
#include <span>

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
	client.SendData<"+CIPSEND", &OnSent>(int16{5}, std::span<const uint8>());
}
