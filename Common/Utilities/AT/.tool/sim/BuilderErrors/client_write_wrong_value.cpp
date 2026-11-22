// expect: Write is given values that do not match the arguments of its write form
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LED">().Write(Argument<bool>("on"));
}>;

using Binding = AtClientBinding<>;

void OnLed(AtStatus status) {
	static_cast<void>(status);
}

void Use(AtClient<Set, Binding>& client) {
	client.Write<"+LED", &OnLed>(2);
}
