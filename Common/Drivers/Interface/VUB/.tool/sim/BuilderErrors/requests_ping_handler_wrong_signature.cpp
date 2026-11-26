// expect: a ping handler has the signature void (ResultStatus, uint32)
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


// The ping handler takes the result only: a ping's handler also gets the round trip.
inline void OnPingResult(ResultStatus) {
}


using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{.open = 1, .ping = true, .pingPadding = 16}>;


inline void SendPing(Layer::Handle& handle) {
	(void)handle.Ping<&OnPingResult>(0);
}
