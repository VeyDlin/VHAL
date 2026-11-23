// expect: VubLink
#include "SnippetSupport.h"


struct NoTraitsLink {
	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<NoTraitsLink>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
