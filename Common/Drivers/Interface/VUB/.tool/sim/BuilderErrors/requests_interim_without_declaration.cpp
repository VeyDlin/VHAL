// expect: Ticket::Interim needs the command to be declared Interim()
#include <Drivers/Interface/VUB/Requests.h>
#include <span>


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


// The handler of a command without Interim() calls Interim: the ticket has no interim call for this command.
inline void OnStop(VUB::Ticket<Set, "motor.stop"> ticket) {
	(void)ticket.Interim(SnippetStatus::done);
}


using Handlers = VUB::Handlers<
	VUB::On<"motor.stop", &OnStop>>;


using Layer = VUB::RequestsOf<Set, Handlers, VUB::RequestsOptions{0, 1, 4}>;


inline void TakeOne(Layer& layer) {
	(void)layer.Take(VUB::Meta(), std::span<const uint8>());
}
