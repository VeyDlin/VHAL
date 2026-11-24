// expect: an OnCancel handler names a command that is not Cancellable()
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>();
}>;


void OnStopCancel(VUB::Ticket<Set, "motor.stop">);

using Handlers = VUB::Handlers<
	VUB::OnCancel<"motor.stop", &OnStopCancel>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
