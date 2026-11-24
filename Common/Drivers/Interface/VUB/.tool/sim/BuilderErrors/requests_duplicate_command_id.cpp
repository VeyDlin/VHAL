// expect: two commands have the same id
#include <Drivers/Interface/VUB/Requests/Commands.h>


struct SnippetBody {
	uint8 value = 0;
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::value>();
	}
};

}


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.start", 0x12>()
			.Request<SnippetBody>()
			.NoReply()
		.Command<"motor.stop", 0x12>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
