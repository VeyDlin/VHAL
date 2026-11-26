// expect: an interim handler names a command that is not Interim() or Cancellable()
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


void OnStopProgress(const VUB::Progress<SnippetStatus>& progress);
static_assert(VUB::CheckProgress<Set, SnippetCommand::stop, &OnStopProgress>());
