// expect: Send needs requester slots
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000);
}>;


inline void OnStopFinal(const VUB::Final<SnippetStatus, VUB::NoBody>&) {
}


// No Open(n): the layer has no requester slots, so a Send has no slot to take.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{0, 0, 0}>;


inline void SendStop(Layer::Handle& handle) {
	(void)handle.Send<"motor.stop", &OnStopFinal>(VUB::NoBody{});
}
