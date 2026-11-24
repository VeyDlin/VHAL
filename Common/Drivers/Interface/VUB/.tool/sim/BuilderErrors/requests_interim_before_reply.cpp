// expect: Interim() needs a Reply<Status>() earlier in the chain
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	start = 0x12
};

enum class SnippetStatus : uint8 {
	done = 0
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
			.Interim()
			.Reply<SnippetStatus, SnippetBody>();
}>;


static_assert(Set::commandCount == 1);
