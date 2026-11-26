// expect: the command needs TicketTimeoutMs
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetCommand : uint8 {
	stop = 0x13
};

enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>();
}>;


// The device takes a ticket for the stop command, and the stop command declares no TicketTimeoutMs: the device could hold the slot for ever.
void OnStop(VUB::Ticket<Set, SnippetCommand::stop>);

using Handlers = VUB::Handlers<
	VUB::On<SnippetCommand::stop, &OnStop>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
