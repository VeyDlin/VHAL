// expect: Cancellable() is for a command with a Reply<Status>() earlier in the chain
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.reset", 0x14>()
			.NoReply()
			.Cancellable();
}>;


static_assert(Set::commandCount == 1);
