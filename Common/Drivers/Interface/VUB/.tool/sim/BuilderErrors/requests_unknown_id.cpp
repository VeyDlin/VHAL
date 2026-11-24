// expect: no command of the set has this id
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	reset = 0x14,
	halt = 0x15
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


using Missing = VUB::CommandOf<Set, SnippetCommand::halt>;


static_assert(Missing::id == 0x15);
