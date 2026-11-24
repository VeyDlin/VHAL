// expect: an event has no Body<T>()
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<"motor.fault", 0x40>()
		.Command<"motor.reset", 0x14>()
			.NoReply();
}>;


static_assert(Set::eventCount == 1);
