// expect: the layer serves a command and has no tickets
#include <Drivers/Interface/VUB/Requests.h>
#include <span>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


void OnStop(VUB::Ticket<Set, "motor.stop">);


using Handlers = VUB::Handlers<
	VUB::On<"motor.stop", &OnStop>>;


// Served, and given no Tickets(n): the default is 0, so a request has no ticket to be held in.
using Layer = VUB::RequestsOf<Set, Handlers>;


inline void TakeOne(Layer& layer) {
	(void)layer.Take(VUB::Meta(), std::span<const uint8>());
}
