// expect: ReplyTimeoutMs must be at most 2147483
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


// The deadline of 2147484 ms is 2147484000 microseconds, past the signed 32-bit difference the layer compares.
enum class SnippetCommand : uint8 {
	stop = 0x13
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2147484);
}>;


static_assert(Set::commandCount == 1);
