// expect: a body field has an integer type that is not fixed width
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetCommand : uint8 {
	start = 0x12
};


struct SnippetBody {
	wchar_t letter = L'a';
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::letter>();
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
