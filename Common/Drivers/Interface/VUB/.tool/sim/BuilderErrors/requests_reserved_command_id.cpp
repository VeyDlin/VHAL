// expect: the ids 0xF0 to 0xFF are reserved for the layer
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"ping", 0xF0>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
