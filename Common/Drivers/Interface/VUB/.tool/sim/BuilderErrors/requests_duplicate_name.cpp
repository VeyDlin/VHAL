// expect: two commands or events have the same name
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.NoReply()
		.Command<"motor.stop", 0x14>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
