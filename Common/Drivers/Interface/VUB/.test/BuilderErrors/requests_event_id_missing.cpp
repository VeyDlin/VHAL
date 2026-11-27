// expect: no event of the set has this id
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetEvent : uint8 {
	fault = 0x40,
	tick = 0x41
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


using Missing = VUB::EventOf<Set, SnippetEvent::tick>;


static_assert(Missing::id == 0x41);
