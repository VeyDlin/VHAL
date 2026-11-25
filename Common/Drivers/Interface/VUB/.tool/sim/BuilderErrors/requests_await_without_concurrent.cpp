// expect: Await needs a concurrent stack
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


// An awaitable layer on a stack that is not concurrent: the Await call has no waiter path to build.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 4, false, false, false, 0, 1000, false, 0, false, 2, true}>;


void Wait(Layer::Handle handle) {
	(void)handle.template Await<SnippetCommand::stop>();
}
