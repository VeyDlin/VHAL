// expect: no command or event of the set has this name
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.reset", 0x14>()
			.NoReply();
}>;


using Missing = VUB::CommandOf<Set, "motor.halt">;


static_assert(Missing::id == 0x14);
