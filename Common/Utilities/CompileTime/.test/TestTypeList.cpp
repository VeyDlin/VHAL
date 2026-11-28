#include <Utilities/CompileTime/TypeList.h>
#include <Utilities/DataTypes.h>
#include <VhalCheck.h>
#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>


// Host tests of the type-list searches. Each answer is a static_assert, and the run-time call of a search with a runtime key is checked
// in main. The compile-fail snippets of the exactly-once search are in BuilderErrors.


enum class CommandId : uint8 {
	ping = 1,
	read = 2,
	write = 3,
};


struct Alpha {
};


struct Beta {
};


struct Gamma {
};


// A command and an event share the id ping: the search on the id must tell the kinds apart.
struct PingCommand {
	using BlockType = Alpha;
	static constexpr CommandId key = CommandId::ping;
	static constexpr bool isCommand = true;
};


struct ReadCommand {
	using BlockType = Beta;
	static constexpr CommandId key = CommandId::read;
	static constexpr bool isCommand = true;
};


struct PingEvent {
	using BlockType = Gamma;
	static constexpr CommandId key = CommandId::ping;
	static constexpr bool isCommand = false;
};


// Holds for the type itself.
template<typename Type>
struct IsType {
	template<typename Element>
	static constexpr bool Holds() {
		return std::is_same_v<Element, Type>;
	}
};


// Holds for an element whose BlockType member is Block.
template<typename Block>
struct HasBlock {
	template<typename Element>
	static constexpr bool Holds() {
		return std::is_same_v<typename Element::BlockType, Block>;
	}
};


// Holds for an element whose key is Value and whose kind is WantCommand, from a constant.
template<auto Value, bool WantCommand>
struct IsKey {
	template<typename Element>
	static constexpr bool Holds() {
		return Element::key == Value && Element::isCommand == WantCommand;
	}
};


// Holds for an element whose key and kind are the values held by the object, which may be set at run time.
struct KeyOf {
	CommandId key;
	bool wantCommand;

	template<typename Element>
	constexpr bool Holds() const {
		return Element::key == key && Element::isCommand == wantCommand;
	}
};


struct Command {
	uint8 id;
};


using Commands = std::tuple<PingCommand, ReadCommand, PingEvent>;


// The first match of a search, and the number of elements when none matches.
static_assert(TypeList::IndexWhere<Commands>(IsKey<CommandId::ping, true>{}) == 0);
static_assert(TypeList::IndexWhere<Commands>(IsKey<CommandId::ping, false>{}) == 2);
static_assert(TypeList::IndexWhere<Commands>(IsKey<CommandId::read, true>{}) == 1);
static_assert(TypeList::IndexWhere<Commands>(IsKey<CommandId::write, true>{}) == 3);
static_assert(TypeList::IndexWhere<Commands>(HasBlock<Gamma>{}) == 2);
static_assert(TypeList::IndexWhere<Commands>(HasBlock<Alpha>{}) == 0);
static_assert(TypeList::IndexWhere<Commands>(HasBlock<Beta>{}) == 1);


// A plain list gives the same answers as a tuple.
static_assert(TypeList::IndexWhere<TypeList::List<Alpha, Beta, Gamma>>(IsType<Beta>{}) == 1);
static_assert(TypeList::IndexWhere<TypeList::List<Alpha, Beta, Gamma>>(IsType<Gamma>{}) == 2);
static_assert(TypeList::IndexWhere<TypeList::List<Alpha, Beta, Gamma>>(IsType<Alpha>{}) == 0);
static_assert(TypeList::IndexWhere<std::tuple<Alpha, Beta, Gamma>>(IsType<Beta>{}) == 1);


// A type that is not in the list gives its size, which is the not-found value.
static_assert(TypeList::IndexWhere<TypeList::List<Alpha, Beta>>(IsType<Gamma>{}) == 2);
static_assert(TypeList::IndexWhere<std::tuple<Alpha, Beta>>(IsType<Gamma>{}) == 2);


// The first of several matches is the one found.
static_assert(TypeList::IndexWhere<std::tuple<Alpha, Beta, Alpha>>(IsType<Alpha>{}) == 0);
static_assert(TypeList::IndexWhere<TypeList::List<Beta, Alpha, Alpha>>(IsType<Alpha>{}) == 1);


// A search on an empty list finds nothing.
static_assert(TypeList::IndexWhere<std::tuple<>>(IsType<Alpha>{}) == 0);
static_assert(TypeList::CountWhere<std::tuple<>>(IsType<Alpha>{}) == 0);


// The number of matches.
static_assert(TypeList::CountWhere<Commands>(IsKey<CommandId::ping, true>{}) == 1);
static_assert(TypeList::CountWhere<Commands>(IsKey<CommandId::ping, false>{}) == 1);
static_assert(TypeList::CountWhere<Commands>(IsKey<CommandId::write, true>{}) == 0);
static_assert(TypeList::CountWhere<std::tuple<Alpha, Beta, Alpha>>(IsType<Alpha>{}) == 2);


// The exactly-once search gives the index of the one match.
static_assert(TypeList::IndexWhereOnce<Commands, HasBlock<Beta>>() == 1);
static_assert(TypeList::IndexWhereOnce<Commands, IsKey<CommandId::read, true>>() == 1);
static_assert(TypeList::IndexWhereOnce<TypeList::List<Alpha, Beta>, IsType<Beta>>() == 1);


// The type search: the index of an exact type, the size when it is absent, and the exactly-once form. A const type is another type.
static_assert(TypeList::IndexOf<Beta, std::tuple<Alpha, Beta, Gamma>>() == 1);
static_assert(TypeList::IndexOf<Gamma, TypeList::List<Alpha, Beta, Gamma>>() == 2);
static_assert(TypeList::IndexOf<Alpha, std::tuple<Alpha, Beta, Alpha>>() == 0);
static_assert(TypeList::IndexOf<Gamma, std::tuple<Alpha, Beta>>() == 2);
static_assert(TypeList::IndexOf<Alpha, std::tuple<>>() == 0);
static_assert(TypeList::IndexOf<const Alpha, std::tuple<Alpha>>() == 1);
static_assert(TypeList::IndexOf<Alpha, std::tuple<const Alpha>>() == 1);
static_assert(TypeList::IndexOfOnce<Beta, std::tuple<Alpha, Beta>>() == 1);
static_assert(TypeList::IndexOfOnce<Gamma, TypeList::List<Gamma>>() == 0);


// A search with a run-time key, where the key is an object's member.
static_assert(TypeList::IndexWhere<Commands>(KeyOf{CommandId::read, true}) == 1);
static_assert(TypeList::IndexWhere<Commands>(KeyOf{CommandId::ping, false}) == 2);
static_assert(TypeList::IndexWhere<Commands>(KeyOf{CommandId::write, false}) == 3);


// The array form: the first item for which the predicate holds, or the array's size.
constexpr std::array<Command, 3> commandTable = {Command{10}, Command{20}, Command{30}};


constexpr std::size_t FindCommand(uint8 id) {
	return TypeList::IndexWhereItem(commandTable, [id](const Command& command) {
		return command.id == id;
	});
}


static_assert(FindCommand(10) == 0);
static_assert(FindCommand(20) == 1);
static_assert(FindCommand(30) == 2);
static_assert(FindCommand(31) == 3);


int main() {
	volatile uint8 rawKey = 2;
	CommandId runtimeKey = static_cast<CommandId>(rawKey);

	VHAL_CHECK_EQ(TypeList::IndexWhere<Commands>(KeyOf{runtimeKey, true}), std::size_t{1});
	VHAL_CHECK_EQ(TypeList::IndexWhere<Commands>(KeyOf{runtimeKey, false}), std::size_t{3});

	volatile uint8 rawId = 20;
	VHAL_CHECK_EQ(FindCommand(rawId), std::size_t{1});
	VHAL_CHECK_EQ(FindCommand(31), std::size_t{3});

	using RepeatedAlpha = std::tuple<Alpha, Beta, Alpha>;
	std::size_t firstAlpha = TypeList::IndexWhere<RepeatedAlpha>(IsType<Alpha>{});
	VHAL_CHECK_EQ(firstAlpha, std::size_t{0});

	return VhalCheck::Finish("TestTypeList");
}
