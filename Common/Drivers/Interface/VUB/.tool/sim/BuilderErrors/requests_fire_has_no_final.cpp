// expect: a Fire command has no final
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetCommand : uint8 {
	kick = 0x01
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::kick>()
			.Fire();
}>;


void OnKickResult(ResultStatus result);
static_assert(VUB::CheckFinal<Set, SnippetCommand::kick, &OnKickResult>());
