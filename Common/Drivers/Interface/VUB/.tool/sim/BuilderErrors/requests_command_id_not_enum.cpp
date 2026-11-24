// expect: a command id is a value of an enum class with a uint8 underlying type
#include <Drivers/Interface/VUB/Requests/Commands.h>


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<0x12>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
