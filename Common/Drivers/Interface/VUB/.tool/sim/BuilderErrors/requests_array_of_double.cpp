// expect: a double field is not in version 1
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <array>


struct SnippetBody {
	std::array<double, 2> steps{};
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::steps>();
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
