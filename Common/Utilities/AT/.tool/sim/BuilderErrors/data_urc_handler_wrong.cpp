// expect: a URC handler does not match the arguments that the description declares for it
#include <Utilities/AT/AtClient.h>

using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;

// The offset of the chunk is missing: the handler of a Data() URC takes the length, the offset, the chunk and the last flag.
void OnIpd(uint16 length, std::span<const uint8> chunk, bool last) {
	static_cast<void>(length);
	static_cast<void>(chunk);
	static_cast<void>(last);
}

using Binding = AtClientBinding<
	AtUrcOn<"+IPD", &OnIpd>
>;

using Client = AtClient<Set, Binding, AtQueue<2>>;

static_assert(sizeof(Client) > 0);
