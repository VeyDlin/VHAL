// expect: Fire takes a command with no reply
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


using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 0}>;


inline void FireStop(Layer::Handle& handle) {
	(void)handle.Fire<"motor.stop">(VUB::NoBody{});
}
