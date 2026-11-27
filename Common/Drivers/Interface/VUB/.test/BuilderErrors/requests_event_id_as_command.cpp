// expect: an event has this id where a command is expected
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetEvent : uint8 {
	fault = 0x40
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
		.Event<SnippetEvent::fault>()
			.Body<SnippetBody>();
}>;


using Wrong = VUB::CommandOf<Set, SnippetEvent::fault>;


static_assert(Wrong::id == 0x40);
