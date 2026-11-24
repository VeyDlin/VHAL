// expect: a command has one reply declaration: Reply, NoReply or Fire
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.Fire();
}>;


static_assert(Set::commandCount == 1);
