// expect: add HighWater() to the stack
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


enum class SnippetCommand : uint8 {
	stop = 0x13
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000);
}>;


void SnippetFill(VUB::NoBody&) {
}


// A layer that sends the high-water marks in its health reply, on a stack that is not HighWater(): the marks would be zero, so the stack
// refuses to build.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Requests<Set, VUB::Handlers<>>>().Counted().AnswerHealth<VUB::NoBody, &SnippetFill>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(8)
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void BuildStack(VUB::PacketEnd<64, 512>& link) {
	Stack stack(link);
	(void)stack;
}
