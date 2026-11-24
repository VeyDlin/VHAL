// expect: a command needs Reply<Status>(), NoReply() or Fire() after its Request()
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
		.Command<"motor.stop", 0x13>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
