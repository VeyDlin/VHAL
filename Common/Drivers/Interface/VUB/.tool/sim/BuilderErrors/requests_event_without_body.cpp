// expect: an event has no Body<T>()
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};

enum class SnippetEvent : uint8 {
	fault = 0x40
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<SnippetEvent::fault>()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


static_assert(Set::eventCount == 1);
