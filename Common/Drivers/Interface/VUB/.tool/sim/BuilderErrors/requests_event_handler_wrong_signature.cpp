// expect: an OnEvent handler is void(const Body&)
#include <Drivers/Interface/VUB/Requests/Handlers.h>


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
		.Event<"motor.fault", 0x40>()
			.Body<SnippetBody>();
}>;


// The event's body is taken by const reference; a value of the wrong type is refused.
void OnFaultByValue(int code);

using Handlers = VUB::Handlers<
	VUB::OnEvent<"motor.fault", &OnFaultByValue>>;


static_assert(VUB::CheckHandlers<Set, Handlers>());
