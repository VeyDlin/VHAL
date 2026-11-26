#pragma once
#include <Utilities/CompileTime/Require.h>
#include "CommandNameTree.h"
#include "CommandTypes.h"
#include <Utilities/DataTypes.h>
#include <Utilities/TextNumber/IntegerParser.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>


// The argument declarations of a command, and the slots that read their text.
//
// Declarations, one per type of argument:
//   Argument<int8>("x").Range(-127, 127).Default(0)   integers: int8 to uint32, int64 and uint64
//   Argument<bool>("on").Default(false)                on, off, true, false, 1, 0
//   Argument<Mode>("mode").Values({{"fast", Mode::Fast}, {"slow", Mode::Slow}})
//   Argument<String<16>>("name").Default("none")       stored text of at most 16 characters
//   Argument<Custom>("value")                          a type with a CommandParser (see CommandParserOf)
//
// An argument with a Default is optional and must come after the required ones.


// An integer type the command line reads. bool and the character types are not integers here.
template<typename T>
concept CommandInteger = std::is_integral_v<T> && !std::is_same_v<T, bool>;


// The one value a declared enum value stands for, under the name the user types.
template<typename E>
struct CommandEnumValue {
	std::string_view name;
	E value;
};


// Where a custom argument type gets its parser. A type names it as CommandParser. A module that adds a type, such as float, can
// specialize this trait instead. The parser has Push(char) returning bool, Pop(), and a const Finish(T&) returning bool that writes
// the value when it returns true. The parser must be trivially destructible.
template<typename T>
struct CommandParserOf {
	using Type = typename T::CommandParser;
};


// True when value, of any integer type, lies inside the range of T.
template<typename T, typename Number>
constexpr bool CommandFitsType(Number value) {
	if constexpr (std::is_signed_v<Number>) {
		if (value < 0) {
			if constexpr (!std::is_signed_v<T>) {
				return false;
			} else {
				return static_cast<int64>(value) >= static_cast<int64>(std::numeric_limits<T>::min());
			}
		}
	}

	return static_cast<uint64>(value) <= static_cast<uint64>(std::numeric_limits<T>::max());
}


// True for a 64-bit unsigned value above the int64 range, which the int64 fields of the metadata cannot hold.
template<typename T>
constexpr bool CommandAboveInt64(T value) {
	if constexpr (std::is_unsigned_v<T> && sizeof(T) == 8) {
		return value > static_cast<T>(std::numeric_limits<int64>::max());
	} else {
		return false;
	}
}


// A 64-bit unsigned value above the int64 range is clamped, so the runtime can report every range in one type. The metadata keeps the
// exact value beside the clamp (see CommandArgumentInfo).
template<typename T>
constexpr int64 CommandToInt64(T value) {
	if (CommandAboveInt64(value)) {
		return std::numeric_limits<int64>::max();
	}

	return static_cast<int64>(value);
}


constexpr std::size_t CommandAlignUp(std::size_t offset, std::size_t alignment) {
	return (offset + alignment - 1) / alignment * alignment;
}


// The fixed words of a boolean argument. Each word is a whole value, and no word is a prefix of another.
struct CommandBoolWord {
	std::string_view word;
	bool value;
};


inline constexpr std::array<CommandBoolWord, 6> CommandBoolWords = {{
	{"on", true},
	{"off", false},
	{"true", true},
	{"false", false},
	{"1", true},
	{"0", false}
}};


// What the runtime and the help text need about one argument. valueOffset is the argument's place in the value area of its command.
// root is the tree of an enum argument in the level. slotBytes and slotAlign size the slot that reads the argument. A 64-bit unsigned
// maximum above the int64 range is kept in unsignedMaximum, with maximumUnsigned set, and maximum holds its clamp.
struct CommandArgumentInfo {
	std::string_view name;
	int64 minimum = 0;
	int64 maximum = 0;
	uint64 unsignedMaximum = 0;
	bool maximumUnsigned = false;
	uint16 root = 0;
	uint16 valueOffset = 0;
	uint16 slotBytes = 0;
	CommandArgumentKind kind = CommandArgumentKind::Custom;
	bool hasDefault = false;
	uint8 valueAlign = 1;
	uint8 slotAlign = 1;
};


// The facts about an argument that do not depend on where it sits. The level adds the offset, the root and the slot size.
template<typename Declaration>
constexpr CommandArgumentInfo CommandDescribe(const Declaration& declaration) {
	CommandArgumentInfo info;
	info.name = declaration.name;
	info.kind = Declaration::Kind;
	info.hasDefault = declaration.hasDefault;

	if constexpr (Declaration::Kind == CommandArgumentKind::Integer) {
		info.minimum = CommandToInt64(declaration.minimum);
		info.maximum = CommandToInt64(declaration.maximum);
		info.maximumUnsigned = CommandAboveInt64(declaration.maximum);

		if (info.maximumUnsigned) {
			info.unsignedMaximum = static_cast<uint64>(declaration.maximum);
		}
	}

	if constexpr (Declaration::Kind == CommandArgumentKind::Text || Declaration::Kind == CommandArgumentKind::Secret) {
		info.maximum = static_cast<int64>(Declaration::MaxLength);
	}

	return info;
}


// The values of an enum argument, as an array of exact size. Values is the only way to make one.
template<typename E, std::size_t Count>
class EnumArgument {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Enumeration;
	using Value = E;

	std::string_view name;
	std::array<CommandEnumValue<E>, Count> values{};
	E defaultValue{};
	bool hasDefault = false;


	constexpr EnumArgument(std::string_view argumentName, std::array<CommandEnumValue<E>, Count> valueList) : name(argumentName), values(valueList) {
	}


	constexpr EnumArgument Default(E value) const {
		EnumArgument copy = *this;
		copy.defaultValue = value;
		copy.hasDefault = true;
		return copy;
	}


	constexpr void Verify() const {
		if (hasDefault) {
			bool found = false;

			for (const CommandEnumValue<E>& item : values) {
				if (item.value == defaultValue) {
					found = true;
				}
			}

			Require<"CommandLine:the default is not one of the enum values">(found);
		}
	}
};


// A declared argument of type T. The primary template is a custom argument type, which reads through its CommandParser. A float
// argument has its own declaration in CommandFloatArguments.h, so a float here means that module is not included.
template<typename T>
class Argument {
	static_assert(!std::is_floating_point_v<T>, "a float argument needs CommandFloatArguments.h, and the FloatArguments module listed in the console");

public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Custom;
	using Value = T;

	std::string_view name;
	T defaultValue{};
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	constexpr Argument Default(T value) const {
		Argument copy = *this;
		copy.defaultValue = value;
		copy.hasDefault = true;
		return copy;
	}


	constexpr void Verify() const {
	}
};


// An integer argument. Range sets the accepted values, which must fit the type. Default makes the argument optional.
template<CommandInteger T>
class Argument<T> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Integer;
	using Value = T;

	std::string_view name;
	T minimum = std::numeric_limits<T>::min();
	T maximum = std::numeric_limits<T>::max();
	T defaultValue{};
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	template<typename Number>
	constexpr Argument Range(Number low, Number high) const {
		static_assert(std::is_integral_v<Number>, "a range takes integers");
		Require<"CommandLine:a range bound is outside the type of the argument">(CommandFitsType<T>(low) && CommandFitsType<T>(high));
		Require<"CommandLine:a range has its minimum above its maximum">(low <= high);

		Argument copy = *this;
		copy.minimum = static_cast<T>(low);
		copy.maximum = static_cast<T>(high);
		return copy;
	}


	template<typename Number>
	constexpr Argument Default(Number value) const {
		static_assert(std::is_integral_v<Number>, "a default takes an integer");
		Require<"CommandLine:the default is outside the type of the argument">(CommandFitsType<T>(value));

		Argument copy = *this;
		copy.defaultValue = static_cast<T>(value);
		copy.hasDefault = true;
		return copy;
	}


	// Checked when the command is declared, because Range and Default may come in either order.
	constexpr void Verify() const {
		Require<"CommandLine:the default is outside the range of the argument">(!hasDefault || (defaultValue >= minimum && defaultValue <= maximum));
	}
};


// A boolean argument. The words are on, off, true, false, 1 and 0.
template<>
class Argument<bool> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Boolean;
	using Value = bool;

	std::string_view name;
	bool defaultValue = false;
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	constexpr Argument Default(bool value) const {
		Argument copy = *this;
		copy.defaultValue = value;
		copy.hasDefault = true;
		return copy;
	}


	constexpr void Verify() const {
	}
};


// A string argument of at most Capacity characters. Default takes text that fits.
template<std::size_t Capacity>
class Argument<String<Capacity>> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Text;
	static constexpr std::size_t MaxLength = Capacity;
	using Value = String<Capacity>;

	std::string_view name;
	String<Capacity> defaultValue{};
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	constexpr Argument Default(std::string_view text) const {
		Require<"CommandLine:the default is longer than the string capacity">(text.size() <= Capacity);

		Argument copy = *this;
		for (std::size_t index = 0; index < text.size(); index++) {
			copy.defaultValue.characters[index] = text[index];
		}

		copy.defaultValue.length = text.size();
		copy.hasDefault = true;
		return copy;
	}


	constexpr void Verify() const {
	}
};


// An enum argument. Values gives the names the user types and the values they stand for, and makes the argument an EnumArgument.
template<typename E>
requires std::is_enum_v<E>
class Argument<E> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Enumeration;
	using Value = E;

	std::string_view name;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	template<std::size_t Count>
	constexpr EnumArgument<E, Count> Values(const CommandEnumValue<E> (&items)[Count]) const {
		std::array<CommandEnumValue<E>, Count> list{};

		for (std::size_t index = 0; index < Count; index++) {
			CommandRequireName(items[index].name);
			Require<"CommandLine:an enum value name is longer than 32 characters">(items[index].name.size() <= CommandMaxNameLength);

			for (std::size_t other = 0; other < index; other++) {
				Require<"CommandLine:duplicate enum names">(items[other].name != items[index].name);
			}

			list[index] = items[index];
		}

		return EnumArgument<E, Count>(name, list);
	}
};


// The reader of an integer argument. The characters go through IntegerParser, which accepts decimal, 0x and 0b, and a sign for a
// signed type. The range is checked when the value is read, at Finish.
template<CommandInteger T>
class CommandIntegerSlot final : public CommandSlot {
private:
	IntegerParser<T> parser;
	std::size_t refused = 0;


public:
	explicit CommandIntegerSlot(const Argument<T>& declaration)
		: parser(IntegerOptions<T>{}.WithHexPrefix().WithBinaryPrefix().WithPlusSign().WithRange(declaration.minimum, declaration.maximum))
	{
	}


	bool Push(char character) override {
		if (refused == 0 && !parser.Push(character).IsError()) {
			return true;
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else {
			parser.Pop();
		}
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0) {
			return CommandFault::Invalid;
		}

		NumberResult<T> result = parser.Finish();
		if (result.error.code == NumberErrorCode::OutOfRange) {
			return CommandFault::OutOfRange;
		}

		if (result.error.IsError()) {
			return CommandFault::Invalid;
		}

		std::memcpy(destination, &result.value, sizeof(T));
		return CommandFault::None;
	}
};


// The reader of a boolean argument. It keeps the characters typed so far, and a character is kept only when some word starts with it.
class CommandBoolSlot final : public CommandSlot {
private:
	static constexpr std::size_t Longest = 5;
	std::array<char, Longest> text{};
	std::size_t length = 0;
	std::size_t refused = 0;


public:
	explicit CommandBoolSlot(const Argument<bool>&) {
	}


	bool Push(char character) override {
		if (refused == 0 && length < Longest) {
			text[length] = character;
			if (IsPrefix(std::string_view(text.data(), length + 1))) {
				length++;
				return true;
			}
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else if (length > 0) {
			length--;
		}
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0) {
			return CommandFault::Invalid;
		}

		std::string_view typed(text.data(), length);
		for (const CommandBoolWord& candidate : CommandBoolWords) {
			if (candidate.word == typed) {
				std::memcpy(destination, &candidate.value, sizeof(bool));
				return CommandFault::None;
			}
		}

		return CommandFault::Invalid;
	}


private:
	static constexpr bool IsPrefix(std::string_view typed) {
		for (const CommandBoolWord& candidate : CommandBoolWords) {
			if (candidate.word.size() >= typed.size() && candidate.word.substr(0, typed.size()) == typed) {
				return true;
			}
		}

		return false;
	}
};


// The reader of a string argument. It stores the characters as typed, quotes included, and refuses those past its capacity. A text
// that starts with a double quote is quoted: inside it, \" and \\ are escapes, a space is part of the text, and the closing quote
// ends it. Finish gives the text without the quotes and with the escapes resolved. Any other text is the value as typed. The parser hides
// the characters of a Secret argument, from the argument's kind.
template<std::size_t Capacity>
class CommandTextSlot final : public CommandSlot {
private:
	String<Capacity> text{};
	std::size_t refused = 0;


public:
	template<typename Declaration>
	explicit CommandTextSlot(const Declaration&) {
	}


	bool Push(char character) override {
		if (refused == 0 && text.length < Capacity) {
			text.characters[text.length] = character;
			text.length++;
			return true;
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else if (text.length > 0) {
			text.length--;
		}
	}


	bool Quoting() const override {
		return text.length > 0 && text.characters[0] == '"' && ClosingQuote() == text.length;
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0) {
			return CommandFault::TooLong;
		}

		String<Capacity> value{};
		CommandFault fault = Decode(value);
		if (fault != CommandFault::None) {
			return fault;
		}

		std::memcpy(destination, &value, sizeof(value));
		return CommandFault::None;
	}


private:
	// The index of the closing quote of a quoted text, or the length of the text when the quote is still open.
	std::size_t ClosingQuote() const {
		bool escape = false;

		for (std::size_t index = 1; index < text.length; index++) {
			if (escape) {
				escape = false;
			} else if (text.characters[index] == '\\') {
				escape = true;
			} else if (text.characters[index] == '"') {
				return index;
			}
		}

		return text.length;
	}


	// Writes the value of the stored text. An open quote is MissingQuote, and text after the closing quote is Invalid.
	CommandFault Decode(String<Capacity>& value) const {
		if (text.length == 0 || text.characters[0] != '"') {
			value = text;
			return CommandFault::None;
		}

		std::size_t closing = ClosingQuote();
		if (closing == text.length) {
			return CommandFault::MissingQuote;
		}

		if (closing + 1 != text.length) {
			return CommandFault::Invalid;
		}

		bool escape = false;
		for (std::size_t index = 1; index < closing; index++) {
			char character = text.characters[index];

			if (escape) {
				escape = false;
				if (character != '"' && character != '\\') {
					Append(value, '\\');
				}

				Append(value, character);
			} else if (character == '\\') {
				escape = true;
			} else {
				Append(value, character);
			}
		}

		return CommandFault::None;
	}


	static void Append(String<Capacity>& value, char character) {
		value.characters[value.length] = character;
		value.length++;
	}
};


// The reader of an enum argument. It walks the value names of the level's tree, one node per character. It keeps the typed
// characters, so Pop walks back from the root, which costs less storage than a node per character.
template<typename E, std::size_t Count>
class CommandEnumSlot final : public CommandSlot {
private:
	std::span<const CommandTreeNode> nodes;
	const EnumArgument<E, Count>& declaration;
	uint16 root;
	uint16 node;
	std::array<char, CommandMaxNameLength> typed{};
	std::size_t depth = 0;
	std::size_t refused = 0;


public:
	CommandEnumSlot(
		const EnumArgument<E, Count>& argument,
		std::span<const CommandTreeNode> treeNodes,
		uint16 treeRoot)
		: nodes(treeNodes), declaration(argument), root(treeRoot), node(treeRoot)
	{
	}


	bool Push(char character) override {
		if (refused == 0 && depth < CommandMaxNameLength) {
			uint16 next = CommandTreeStep(nodes, node, character);
			if (next != CommandNoNode) {
				typed[depth] = character;
				depth++;
				node = next;
				return true;
			}
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else if (depth > 0) {
			depth--;
			node = root;
			for (std::size_t index = 0; index < depth; index++) {
				node = CommandTreeStep(nodes, node, typed[index]);
			}
		}
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0 || depth == 0) {
			return CommandFault::Invalid;
		}

		uint16 payload = nodes[node].payload;
		if (payload == CommandNoNode) {
			return CommandFault::Invalid;
		}

		E value = declaration.values[payload].value;
		std::memcpy(destination, &value, sizeof(E));
		return CommandFault::None;
	}


	// The node of the tree that the text read so far stands at. Returns false when the text read so far is refused. Completion reads it
	// through CommandSet::EnumNode, which knows the slot's type, so the slot base has no virtual member for it.
	bool TreeNode(uint16& found) const {
		if (refused > 0) {
			return false;
		}

		found = node;
		return true;
	}
};


// The reader of a custom argument type, through the parser named by CommandParserOf.
template<typename T>
class CommandCustomSlot final : public CommandSlot {
	static_assert(std::is_trivially_destructible_v<typename CommandParserOf<T>::Type>, "a custom argument parser must be trivially destructible");

private:
	typename CommandParserOf<T>::Type parser;
	std::size_t refused = 0;


public:
	explicit CommandCustomSlot(const Argument<T>&) : parser() {
	}


	bool Push(char character) override {
		if (refused == 0 && parser.Push(character)) {
			return true;
		}

		refused++;
		return false;
	}


	void Pop() override {
		if (refused > 0) {
			refused--;
		} else {
			parser.Pop();
		}
	}


	CommandFault Finish(std::byte* destination) const override {
		if (refused > 0) {
			return CommandFault::Invalid;
		}

		T value{};
		if (!parser.Finish(value)) {
			return CommandFault::Invalid;
		}

		std::memcpy(destination, &value, sizeof(T));
		return CommandFault::None;
	}
};


// The slot type that reads a declaration. The primary template is the custom one.
template<typename Declaration>
struct CommandSlotOf {
	using Type = CommandCustomSlot<typename Declaration::Value>;
};


template<CommandInteger T>
struct CommandSlotOf<Argument<T>> {
	using Type = CommandIntegerSlot<T>;
};


template<>
struct CommandSlotOf<Argument<bool>> {
	using Type = CommandBoolSlot;
};


template<std::size_t Capacity>
struct CommandSlotOf<Argument<String<Capacity>>> {
	using Type = CommandTextSlot<Capacity>;
};


template<typename E, std::size_t Count>
struct CommandSlotOf<EnumArgument<E, Count>> {
	using Type = CommandEnumSlot<E, Count>;
};
