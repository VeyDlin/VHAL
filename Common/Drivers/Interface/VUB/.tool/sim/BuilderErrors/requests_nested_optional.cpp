// expect: a nested body has an Optional field
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	start = 0x12
};


struct SnippetInner {
	uint8 a = 0;
	uint8 b = 0;
};

struct SnippetBody {
	SnippetInner inner{};
};


namespace VUB {

template<>
struct BodyOf<SnippetInner> {
	static consteval auto Layout() {
		return Fields<SnippetInner>()
			.Field<&SnippetInner::a>()
			.Optional<&SnippetInner::b>();
	}
};


template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::inner>();
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
