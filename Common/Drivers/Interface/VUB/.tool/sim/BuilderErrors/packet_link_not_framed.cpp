// expect: a packet link must be Framed()
#include "SnippetSupport.h"


struct UnframedPacketLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using Stack = Vub<+[] {
	return VubBuilder()
		.Layer<TestLayer>()
		.Link<UnframedPacketLink>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
