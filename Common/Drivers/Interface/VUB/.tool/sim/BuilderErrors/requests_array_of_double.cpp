// expect: a double field is not in version 1
#include <Drivers/Interface/VUB/Requests/Commands.h>
#include <array>


enum class SnippetCommand : uint8 {
	start = 0x12
};


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
		.Command<SnippetCommand::start>()
			.Request<SnippetBody>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
