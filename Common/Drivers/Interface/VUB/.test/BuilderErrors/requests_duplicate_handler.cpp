// expect: two handlers of one kind name the same command or event
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


void OnResetA();
void OnResetB();

using Handlers = VUB::Handlers<
	VUB::On<SnippetCommand::reset, &OnResetA>,
	VUB::On<SnippetCommand::reset, &OnResetB>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
