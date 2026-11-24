// expect: a Fire command has no final
#include <Drivers/Interface/VUB/Requests/Handlers.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"kick", 0x01>()
			.Fire();
}>;


void OnKickResult(ResultStatus result);


static_assert(VUB::CheckFinal<Set, "kick", &OnKickResult>());
