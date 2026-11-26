// expect: two commands have the same id
#include <Drivers/Interface/VUB/Requests/Commands.h>


// Two commands of one enum with one value: the value is the id on the wire.
enum class SnippetCommand : uint8 {
	start = 0x12,
	stop = 0x12
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
			.NoReply()
		.Command<SnippetCommand::stop>()
			.NoReply();
}>;


static_assert(Set::commandCount == 2);
