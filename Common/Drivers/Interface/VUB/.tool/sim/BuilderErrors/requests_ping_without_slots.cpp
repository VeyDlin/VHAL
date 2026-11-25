// expect: Ping needs requester slots
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


enum class SnippetCommand : uint8 {
	stop = 0x13
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::stop>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000);
}>;


inline void OnPingDone(ResultStatus, uint32) {
}


// Ping on, and no Open(n): the layer has no slot to send a ping from.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{.ping = true, .pingPadding = 16}>;


inline void SendPing(Layer::Handle& handle) {
	(void)handle.Ping<&OnPingDone>(0);
}
