// expect: the final of a command with a reply is void(const Final<Status, Reply>&)
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


// The final of a reply carries the status and the body: a result alone is the final of a NoReply command.
void OnStopResult(ResultStatus result);
static_assert(VUB::CheckFinal<Set, SnippetCommand::stop, &OnStopResult>());
