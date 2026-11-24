// expect: the name is of an event where a command is expected
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


using Set = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<"motor.fault", 0x40>()
			.Body<SnippetBody>();
}>;


using Wrong = VUB::CommandOf<Set, "motor.fault">;


static_assert(Wrong::id == 0x40);
