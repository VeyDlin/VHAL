// expect: a command id is a value of an enum class with a uint8 underlying type
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class WideCommand : uint16 {
	start = 0x12
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<WideCommand::start>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
