// expect: an event id is 0xF0 or more
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class SnippetEvent : uint8 {
	fault = 0xF1
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<SnippetEvent::fault>()
			.Body<VUB::NoBody>();
}>;


static_assert(Set::eventCount == 1);
