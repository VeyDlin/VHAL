// expect: the MaxPacket of a link is zero
#include "SnippetSupport.h"


struct ZeroLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(0).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<ZeroLink>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
