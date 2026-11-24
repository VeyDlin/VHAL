// expect: CancelAll needs a command that is Cancellable()
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
			.Reply<SnippetStatus>();
}>;


using Layer = VUB::Requests<Set, VUB::Handlers<>>;


void CancelEverything(Layer::Handle handle) {
	(void)handle.CancelAll();
}
