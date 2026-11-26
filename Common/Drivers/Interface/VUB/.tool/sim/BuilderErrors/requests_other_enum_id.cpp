// expect: no command of the set has this id
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};

// The same value in another enum: the type of the enum is part of the id.
enum class OtherCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


using Wrong = VUB::CommandOf<Set, OtherCommand::reset>;


static_assert(Wrong::id == 0x14);
