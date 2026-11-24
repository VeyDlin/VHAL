// expect: a reply status is an enum class with a uint8 underlying type
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	stop = 0x13
};

enum class WideStatus : uint16 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<WideStatus>();
}>;


static_assert(Set::commandCount == 1);
