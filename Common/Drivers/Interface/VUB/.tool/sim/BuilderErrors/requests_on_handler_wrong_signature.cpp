// expect: an On handler of a command with a reply is void(Ticket, const Body&)
#include <Drivers/Interface/VUB/Requests/Handlers.h>


enum class SnippetCommand : uint8 {
	start = 0x12
};

enum class SnippetStatus : uint8 {
	done = 0
};


struct SnippetBody {
	uint8 value = 0;
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::value>();
	}
};

}


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<SnippetCommand::start>()
			.Request<SnippetBody>()
			.Reply<SnippetStatus>();
}>;


// The body is missing: a command with a reply and a request body takes the ticket and the body.
void OnStartNoTicket(const SnippetBody& body);

using Handlers = VUB::Handlers<
	VUB::On<SnippetCommand::start, &OnStartNoTicket>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
