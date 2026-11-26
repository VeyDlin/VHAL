// expect: a request body type has no BodyOf
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	start = 0x12
};


struct NoLayout {
	uint8 value = 0;
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::start>()
			.Request<NoLayout>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
