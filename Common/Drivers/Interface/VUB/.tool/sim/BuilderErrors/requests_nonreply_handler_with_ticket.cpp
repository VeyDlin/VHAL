// expect: an On handler of a NoReply or Fire command with no request body is void()
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


// A NoReply command has no ticket: its handler takes nothing (the body, when it has one).
void OnResetWithTicket(VUB::Ticket<Set, SnippetCommand::reset>);

using Handlers = VUB::Handlers<
	VUB::On<SnippetCommand::reset, &OnResetWithTicket>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
