// expect: an On handler of a NoReply or Fire command with no request body is void()
#include <Drivers/Interface/VUB/Requests/Handlers.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.reset", 0x14>()
			.NoReply();
}>;


// A NoReply command has no ticket: its handler takes nothing (the body, when it has one).
void OnResetWithTicket(VUB::Ticket<Set, "motor.reset">);

using Handlers = VUB::Handlers<
	VUB::On<"motor.reset", &OnResetWithTicket>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
