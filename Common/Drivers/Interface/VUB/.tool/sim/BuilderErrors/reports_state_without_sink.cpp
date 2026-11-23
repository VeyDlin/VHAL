// expect: needs SetStateSink
#include "SnippetSupport.h"


// A link that says it reports its state, but has no SetStateSink for the stack to give its state object to.
struct SilentReporter {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).FullDuplex().Framed().ReportsState();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<TestLayer>()
		.Link<SilentReporter>()
		.MaxPayload(40)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


static_assert(Stack::count == 2);
