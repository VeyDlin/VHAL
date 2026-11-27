// expect: AwaitSendData needs AtDataMode among the modules of the set
#include <Utilities/AT/AtClient.h>
#include <span>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Finals("SEND OK");
}>;

using Binding = AtClientBinding<>;

void Use(AtClient<Set, Binding, AtQueue<2>, AtAwait>& client) {
	static_cast<void>(client.AwaitSendData<"+CIPSEND">(uint16{5}, std::span<const uint8>()));
}
