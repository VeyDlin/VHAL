// expect: a command with a delivery outcome needs Reliable below Requests; use .Fire() for no delivery at all
#include <Drivers/Interface/VUB/Requests.h>


enum class SnippetStatus : uint8 {
	done = 0
};


enum class SnippetCommand : uint8 {
	reset = 0x14
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::reset>()
			.NoReply();
}>;


inline void OnReset(ResultStatus) {
}


// The layer sits on a link with no Reliable below it: it has no outcomes, so a command with a NoReply has no delivery to report.
using Layer = VUB::RequestsOf<Set, VUB::Handlers<>, VUB::RequestsOptions{1, 0, 0, false}>;


inline void SendReset(Layer& layer, const VUB::Outbox& outbox) {
	(void)layer.template StartRequest<SnippetCommand::reset, &OnReset, nullptr>(VUB::NoBody{}, outbox);
}
