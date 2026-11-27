// expect: Await needs the requests layer's Step .Awaitable()
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


// A concurrent layer with two packets of queue, and no Awaitable(): its handle has no Await.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 4, false, false, false, 0, 1000, false, 0, true, 2, false}>;


void Wait(Layer::Handle handle) {
	(void)handle.template Await<SnippetCommand::stop>();
}
