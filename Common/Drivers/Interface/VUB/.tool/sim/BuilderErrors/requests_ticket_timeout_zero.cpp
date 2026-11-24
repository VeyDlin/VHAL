// expect: TicketTimeoutMs must be above 0
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.TicketTimeoutMs(0);
}>;


static_assert(Set::commandCount == 1);
