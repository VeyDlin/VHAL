// expect: a double field is not in version 1
#include <Drivers/Interface/VUB/Requests/Commands.h>


struct SnippetBody {
	double gain = 0.0;
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::gain>();
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
