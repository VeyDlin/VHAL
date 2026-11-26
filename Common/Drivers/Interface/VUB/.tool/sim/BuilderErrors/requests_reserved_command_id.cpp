// expect: the ids 0xF0 to 0xFF are reserved for the layer
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	ping = 0xF0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::ping>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
