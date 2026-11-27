// expect: a command needs Reply<Status>(), NoReply() or Fire() after its Request()
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	start = 0x12,
	stop = 0x13
};


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
		.Command<SnippetCommand::start>()
			.Request<SnippetBody>()
		.Command<SnippetCommand::stop>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
