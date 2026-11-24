// expect: ReplyTimeoutMs must be above 0
#include <Drivers/Interface/VUB/Requests/Commands.h>


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


enum class SnippetStatus : uint8 {
	done = 0
};


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.stop", 0x13>()
			.Reply<SnippetStatus>()
			.ReplyTimeoutMs(0);
}>;


static_assert(Set::commandCount == 1);
