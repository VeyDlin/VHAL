// expect: the link must say HalfDuplex() or FullDuplex()
#include "SnippetSupport.h"


struct NoDuplexLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64);


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<NoDuplexLink>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
