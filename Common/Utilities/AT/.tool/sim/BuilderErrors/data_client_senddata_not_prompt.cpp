// expect: SendData is for a command with .Prompt()
#include <Utilities/AT/AtClient.h>
#include <span>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+LED">().Write(Argument<bool>("on"));
}>;

using Binding = AtClientBinding<>;

void OnSent(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding, AtQueue<2>>& client) {
	client.SendData<"+LED", &OnSent>(true, std::span<const uint8>());
}
