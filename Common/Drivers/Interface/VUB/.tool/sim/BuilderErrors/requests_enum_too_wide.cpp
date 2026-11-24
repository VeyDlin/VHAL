// expect: an enum field is at most 4 bytes
#include <Drivers/Interface/VUB/Requests/Commands.h>


enum class WideMode : uint64 {
	off = 0
};


struct SnippetBody {
	WideMode mode = WideMode::off;
};


namespace VUB {

template<>
struct BodyOf<SnippetBody> {
	static consteval auto Layout() {
		return Fields<SnippetBody>().Field<&SnippetBody::mode>();
	}
};

}


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<"motor.start", 0x12>()
			.Request<SnippetBody>()
			.NoReply();
}>;


static_assert(Set::commandCount == 1);
