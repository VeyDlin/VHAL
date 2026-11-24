// expect: a command with a reply needs ReplyTimeoutMs
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>();
}>;


inline void OnStopFinal(const VUB::Final<SnippetStatus, VUB::NoBody>&) {
}


using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 0}>;


inline void SendStop(Layer::Handle& handle) {
	(void)handle.Send<"motor.stop", &OnStopFinal>(VUB::NoBody{});
}
