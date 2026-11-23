// expect: a stream link cannot be Framed()
#include "SnippetSupport.h"


struct FramedStreamLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).HalfDuplex();


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Framing<FlagFraming>()
		.Link<FramedStreamLink>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 3);
