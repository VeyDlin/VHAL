// expect: add Counted() to the Reliable layer
#include "SnippetSupport.h"
#include <Drivers/Interface/VUB/Layer/Reliable.h>
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


// A health reply's counters include Reliable's resends and peer restarts. Reliable below the layer is not Counted(), so the stack refuses it.
using Stack = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::Requests<Set, VUB::Handlers<>>>().Counted().AnswerHealth<VUB::NoBody, &SnippetFill>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<VUB::PacketEnd<64, 512>>()
		.MaxPayload(40)
		.HighWater()
		.OnReceive<&SnippetReceive>()
		.Clock<&SnippetClock>();
}>;


inline void BuildStack(VUB::PacketEnd<64, 512>& link) {
	Stack stack(link);
	(void)stack;
}
