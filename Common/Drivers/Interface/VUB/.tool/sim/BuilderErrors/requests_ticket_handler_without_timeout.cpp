// expect: the command needs TicketTimeoutMs
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>();
}>;


// The device takes a ticket for motor.stop, and motor.stop declares no TicketTimeoutMs: the device could hold the slot for ever.
void OnStop(VUB::Ticket<Set, "motor.stop">);

using Handlers = VUB::Handlers<
	VUB::On<"motor.stop", &OnStop>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
