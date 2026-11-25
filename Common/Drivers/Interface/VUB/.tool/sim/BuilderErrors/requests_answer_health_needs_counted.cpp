// expect: AnswerHealth needs this layer's Counted()
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


// A layer that answers health requests with no Counted() on the layer: the reply's counters would have no refused, lateReplies or
// eventsLost to send, so the layer refuses to build.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Requests<Set, VUB::Handlers<>>>().AnswerHealth<VUB::NoBody, &SnippetFill>()
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(8)
		.HighWater()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void BuildStack(VUB::PacketEnd<64, 512>& link) {
	Stack stack(link);
	(void)stack;
}
