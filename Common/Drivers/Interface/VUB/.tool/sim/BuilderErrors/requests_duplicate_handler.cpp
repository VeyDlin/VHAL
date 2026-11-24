// expect: two handlers of one kind name the same command or event
#include <Drivers/Interface/VUB/Requests/Handlers.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.reset", 0x14>()
			.NoReply();
}>;


void OnResetA();
void OnResetB();

using Handlers = VUB::Handlers<
	VUB::On<"motor.reset", &OnResetA>,
	VUB::On<"motor.reset", &OnResetB>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
