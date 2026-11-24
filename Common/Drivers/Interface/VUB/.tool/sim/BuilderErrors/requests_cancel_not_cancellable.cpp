// expect: an OnCancel handler names a command that is not Cancellable()
#include <Drivers/Interface/VUB/Requests/Handlers.h>


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


void OnStopCancel(VUB::Ticket<Set, SnippetCommand::stop>);

using Handlers = VUB::Handlers<
	VUB::OnCancel<SnippetCommand::stop, &OnStopCancel>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
