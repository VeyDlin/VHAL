// expect: a required field follows an Optional field
#include <Drivers/Interface/VUB/Requests/Commands.h>


struct SnippetBody {
	uint16 rampMs = 300;
	uint8 direction = 0;
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>()
			.Optional<&SnippetBody::rampMs>()
			.Field<&SnippetBody::direction>();
	}
};

}


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.start", 0x12>()
			.Request<SnippetBody>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
