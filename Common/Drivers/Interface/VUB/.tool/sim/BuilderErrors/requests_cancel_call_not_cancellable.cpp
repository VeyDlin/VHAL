// expect: Cancel names a command that is not Cancellable()
#include <Drivers/Interface/VUB/Layer/Requests.h>


enum class SnippetCommand : uint8 {
	start = 0x12,
	stop = 0x13
};

enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::start>()
			.Reply<SnippetStatus>()
			.Cancellable()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>();
}>;


using Layer = VUB::Requests<Set, VUB::Handlers<>>;


void CancelStop(Layer::Handle handle, VUB::RequestId id) {
	(void)handle.Cancel<SnippetCommand::stop>(id);
}
