// expect: an event id is 0xF0 or more
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<"motor.fault", 0xF1>()
			.Body<VUB::NoBody>();
}>;


static_assert(Set::eventCount == 1);
