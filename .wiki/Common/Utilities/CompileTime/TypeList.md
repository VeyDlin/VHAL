# TypeList

Searches over a list of types, at compile time. The code is in `Common/Utilities/CompileTime/TypeList.h`, in the namespace `TypeList`. A list is a `std::tuple<...>` or a `TypeList::List<...>`.

| Function | Result |
|---|---|
| `TypeList::IndexWhere<Container>(predicate)` | the index of the first element for which the predicate holds, or the number of elements when none does |
| `TypeList::CountWhere<Container>(predicate)` | the number of elements for which the predicate holds |
| `TypeList::IndexWhereOnce<Container, Predicate>()` | the index of the only element for which the predicate holds; a compile error that names the predicate when there are none or more than one |
| `TypeList::IndexWhereItem(array, predicate)` | the index of the first item of a `std::array` for which the predicate holds, or the array's size |

A predicate is a type or an object with a member template `Holds<Element>()` that gives a `bool`. `Holds` is called for each element type. A predicate that compares a value keeps it as a template argument, or as a member of the object.

## Predicates

A predicate on the type itself:

```cpp
#include <Utilities/CompileTime/TypeList.h>
#include <tuple>
#include <type_traits>

template<typename Type>
struct IsType {
	template<typename Element>
	static constexpr bool Holds() {
		return std::is_same_v<Element, Type>;
	}
};

static_assert(TypeList::IndexWhere<std::tuple<int, char, float>>(IsType<char>{}) == 1);
static_assert(TypeList::IndexWhere<std::tuple<int, char>>(IsType<double>{}) == 2);
```

The second line is the not-found value: the size of the tuple.

A predicate on a member of each element, here a type named `BlockType`:

```cpp
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
```

A predicate on a constant member, such as a command id. The id is a template argument, so two elements with the same id but a different kind can be told apart:

```cpp
#include <Utilities/CompileTime/TypeList.h>
#include <Utilities/DataTypes.h>

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
```

A predicate with a run-time key keeps the key in the object. The object is built at run time when its key is not known at compile time:

```cpp
struct KeyOf {
	Id key;
	bool wantCommand;

	template<typename Element>
	constexpr bool Holds() const {
		return Element::key == key && Element::isCommand == wantCommand;
	}
};

constexpr std::size_t found = TypeList::IndexWhere<Items>(KeyOf{Id::ping, false});  // 1
```

## Lists

A plain list of types gives the same answers as a tuple:

```cpp
static_assert(TypeList::IndexWhere<TypeList::List<int, char, float>>(IsType<float>{}) == 2);
static_assert(TypeList::CountWhere<std::tuple<char, int, char>>(IsType<char>{}) == 2);
```

## Exactly once

`IndexWhereOnce` is for a lookup that must find one element. The predicate is default-constructed, so its value is in its type:

```cpp
static_assert(TypeList::IndexWhereOnce<Items, IsKey<Id::ping, true>>() == 0);
```

When no element or two or more elements hold, the call does not compile. The diagnostic names the predicate and shows the count as the `Value` argument:

```text
error: call to non-'constexpr' function 'void ExactlyOnceFailed() [with Predicate = HasBlock<char>; long unsigned int Value = 0]'
```

## Arrays

`IndexWhereItem` searches a table of values, such as a table of commands looked up by id. The predicate is a lambda or a function object that takes a const reference to an item:

```cpp
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
```

## Cost

The searches are `constexpr`. A search whose result is a constant is evaluated at compile time and adds no code. A search called at run time is compiled into the caller, so only the searches a program calls at run time take space.
