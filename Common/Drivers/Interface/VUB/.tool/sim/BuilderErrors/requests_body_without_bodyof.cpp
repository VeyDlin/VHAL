// expect: a request body type has no BodyOf
#include <Drivers/Interface/VUB/Requests/Commands.h>


struct NoLayout {
	uint8 value = 0;
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.start", 0x12>()
			.Request<NoLayout>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
