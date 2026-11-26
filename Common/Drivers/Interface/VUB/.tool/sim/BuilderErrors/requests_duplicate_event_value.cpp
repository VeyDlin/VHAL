// expect: two events have the same id value
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetEvent : uint8 {
	fault = 0x40,
	tick = 0x40
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<SnippetEvent::fault>()
			.Body<VUB::NoBody>()
		.Event<SnippetEvent::tick>()
			.Body<VUB::NoBody>();
}>;


static_assert(Set::eventCount == 2);
