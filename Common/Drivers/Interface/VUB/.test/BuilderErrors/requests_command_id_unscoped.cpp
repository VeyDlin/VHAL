// expect: a command id is a value of an enum class with a uint8 underlying type
#include <Drivers/Interface/VUB/Requests/Commands.h>


// An unscoped enum with a uint8 underlying type is not an enum class.
enum Unscoped : uint8 {
	start = 0x12
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<start>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
