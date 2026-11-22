// expect: a URC handler does not match the arguments that the description declares for it
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Urc<"+IPD">(Argument<uint16>("length"), Argument<bool>("flag"));
}>;

void OnIpd(uint16 length) {
	static_cast<void>(length);
}

using Binding = AtClientBinding<AtUrcOn<"+IPD", &OnIpd>>;

using Client = AtClient<Set, Binding>;

static_assert(sizeof(Client) > 0);
