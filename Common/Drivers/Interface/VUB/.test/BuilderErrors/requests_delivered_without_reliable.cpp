// expect: an event with Delivered() needs Reliable below Requests
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetEvent : uint8 {
	alarm = 0x42
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<SnippetEvent::alarm>()
			.Body<VUB::NoBody>()
			.Delivered();
}>;


// The layer sits on a link with no Reliable below it: a Delivered event has no sequence to be sent with.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{0, 0, 0, false}>;


inline void EmitAlarm(Layer& layer, const VUB::Outbox& outbox) {
	typename Layer::Handle handle(layer, outbox);
	(void)handle.template Emit<SnippetEvent::alarm>();
}
