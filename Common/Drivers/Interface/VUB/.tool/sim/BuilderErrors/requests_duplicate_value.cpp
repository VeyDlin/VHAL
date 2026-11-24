// expect: two commands have the same id value
#include <Drivers/Interface/VUB/Requests/Commands.h>


// Two enums, each with a value 0x13: the id on the wire is the byte, so the two commands are one id.
enum class FirstCommand : uint8 {
	stop = 0x13
};

enum class SecondCommand : uint8 {
	stop = 0x13
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<FirstCommand::stop>()
			.NoReply()
		.Command<SecondCommand::stop>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
