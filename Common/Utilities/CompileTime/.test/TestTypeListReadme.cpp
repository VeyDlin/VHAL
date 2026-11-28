#include <Utilities/CompileTime/TypeList.h>
#include <Utilities/DataTypes.h>
#include <VhalCheck.h>
#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>


// The examples of the TypeList wiki page, as the page writes them, each in its own namespace so that they compile as written. A check
// follows the examples whose result the page states.


namespace TypeExample {
	template<typename Type>
	struct IsType {
		template<typename Element>
		static constexpr bool Holds() {
			return std::is_same_v<Element, Type>;
		}
	};

	static_assert(TypeList::IndexWhere<std::tuple<int, char, float>>(IsType<char>{}) == 1);
	static_assert(TypeList::IndexWhere<std::tuple<int, char>>(IsType<double>{}) == 2);
}


namespace TypeOfExample {
	static_assert(TypeList::IndexOf<char, std::tuple<int, char, float>>() == 1);
	static_assert(TypeList::IndexOf<double, std::tuple<int, char>>() == 2);
	static_assert(TypeList::IndexOf<const char, std::tuple<char>>() == 1);
	static_assert(TypeList::IndexOfOnce<char, TypeList::List<int, char, float>>() == 1);
}


namespace BlockExample {
	template<typename Block>
	struct HasBlock {
		template<typename Element>
		static constexpr bool Holds() {
			return std::is_same_v<typename Element::BlockType, Block>;
		}
	};

	struct Pump {
		using BlockType = int;
	};

	struct Valve {
		using BlockType = char;
	};

	static_assert(TypeList::IndexWhere<std::tuple<Pump, Valve>>(HasBlock<char>{}) == 1);
}


namespace KeyExample {
	enum class Id : uint8 {
		ping = 1,
		read = 2,
	};

	template<auto Value, bool WantCommand>
	struct IsKey {
		template<typename Element>
		static constexpr bool Holds() {
			return Element::key == Value && Element::isCommand == WantCommand;
		}
	};

	struct PingCommand {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = true;
	};

	struct PingEvent {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = false;
	};

	using Items = std::tuple<PingCommand, PingEvent>;

	static_assert(TypeList::IndexWhere<Items>(IsKey<Id::ping, true>{}) == 0);
	static_assert(TypeList::IndexWhere<Items>(IsKey<Id::ping, false>{}) == 1);
}


namespace RunTimeKeyExample {
	enum class Id : uint8 {
		ping = 1,
		read = 2,
	};

	struct PingCommand {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = true;
	};

	struct PingEvent {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = false;
	};

	using Items = std::tuple<PingCommand, PingEvent>;

	struct KeyOf {
		Id key;
		bool wantCommand;

		template<typename Element>
		constexpr bool Holds() const {
			return Element::key == key && Element::isCommand == wantCommand;
		}
	};

	constexpr std::size_t found = TypeList::IndexWhere<Items>(KeyOf{Id::ping, false});
	static_assert(found == 1);
}


namespace ListExample {
	template<typename Type>
	struct IsType {
		template<typename Element>
		static constexpr bool Holds() {
			return std::is_same_v<Element, Type>;
		}
	};

	static_assert(TypeList::IndexWhere<TypeList::List<int, char, float>>(IsType<float>{}) == 2);
	static_assert(TypeList::CountWhere<std::tuple<char, int, char>>(IsType<char>{}) == 2);
}


namespace OnceExample {
	enum class Id : uint8 {
		ping = 1,
	};

	template<auto Value, bool WantCommand>
	struct IsKey {
		template<typename Element>
		static constexpr bool Holds() {
			return Element::key == Value && Element::isCommand == WantCommand;
		}
	};

	struct PingCommand {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = true;
	};

	struct PingEvent {
		static constexpr Id key = Id::ping;
		static constexpr bool isCommand = false;
	};

	using Items = std::tuple<PingCommand, PingEvent>;

	static_assert(TypeList::IndexWhereOnce<Items, IsKey<Id::ping, true>>() == 0);
}


namespace ArrayExample {
	struct Command {
		uint8 id;
	};

	constexpr std::array<Command, 3> commands = {Command{10}, Command{20}, Command{30}};

	constexpr std::size_t FindCommand(uint8 id) {
		return TypeList::IndexWhereItem(commands, [id](const Command& command) {
			return command.id == id;
		});
	}

	static_assert(FindCommand(20) == 1);
	static_assert(FindCommand(31) == 3);
}


int main() {
	VHAL_CHECK_EQ(RunTimeKeyExample::found, std::size_t{1});
	VHAL_CHECK_EQ(ArrayExample::FindCommand(20), std::size_t{1});
	return VhalCheck::Finish("TestTypeListReadme");
}
