// expect: TicketTimeoutMs must be above 0
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	stop = 0x13
};

enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>()
			.TicketTimeoutMs(0);
}>;


static_assert(Set::commandCount == 1);
