// expect: Pending needs at least 1 packet
#include <Drivers/Interface/VUB/Layer/Requests.h>


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
			.ReplyTimeoutMs(2000);
}>;


// A concurrent layer that sends, with a queue of zero packets.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 4, false, false, false, 0, 1000, false, 0, true, 0, false}>;


Layer layer{};
