// expect: BuiltInTimeoutMs needs the Step .Ping(maxPadding)
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


enum class SnippetCommand : uint8 {
	stop = 0x13
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000);
}>;


// A deadline for the built-in requests, with no ping on the layer: the deadline would have nothing to bound.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{.open = 1, .builtInTimeoutMs = 300}>;


inline Layer::Options* unusedOptions = nullptr;
