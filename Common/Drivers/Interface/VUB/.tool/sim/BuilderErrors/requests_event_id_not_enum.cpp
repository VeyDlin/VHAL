// expect: an event id is a value of an enum class with a uint8 underlying type
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<0x40>()
			.Body<VUB::NoBody>();
}>;


static_assert(Set::eventCount == 1);
