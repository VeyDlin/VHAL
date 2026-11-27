// expect: Cancellable() is for a command with a Reply<Status>() earlier in the chain
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply()
			.Cancellable();
}>;


static_assert(Set::commandCount == 1);
