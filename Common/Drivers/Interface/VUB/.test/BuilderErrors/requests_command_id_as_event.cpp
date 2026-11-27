// expect: a command has this id where an event is expected
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


using Wrong = VUB::EventOf<Set, SnippetCommand::reset>;


static_assert(Wrong::id == 0x14);
