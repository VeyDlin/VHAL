#pragma once
#include "CommandArgument.h"
#include "CommandLevel.h"
#include "CommandNameTree.h"
#include "CommandTypes.h"
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>


// The streaming parser of a console. It keeps no line. Each character moves the state of its position, and the state of every token
// on the line is kept in place, so Backspace can return to it:
//   - a command or group name keeps its tree node at each character (one row per level, in nameNodes);
//   - an argument keeps its reader in its own slot, and the count of its characters;
//   - a gap keeps the spaces typed after the last token, and what the gap follows.
// A character that cannot be parsed stops the parse: the state is frozen and the rest of the line is counted in tail, which Backspace
// removes until the parse resumes where it stopped.
//
// Without commands (the default-only build) the parser keeps only the current word, for the Word events, and the spaces around it.


// The size of an array of the set, which is one element when the set has none.
constexpr std::size_t CommandArraySize(std::size_t count) {
	return count > 0 ? count : 1;
}


// The characters a word keeps for messages and Word events. A longer word is cut there, and no more.
inline constexpr std::size_t CommandWordSize = 16;


// Where the parse is inside the line: in a name, in an argument, or in the gap after a token (or at the start).
enum class CommandPhase : uint8 {
	Name,
	Argument,
	Gap
};


// What the gap follows: nothing (the start of the line), a group name, the command name, or an argument.
enum class CommandAfter : uint8 {
	Start,
	Group,
	Command,
	Argument
};


// Why the parse stopped. Unknown is a name that matches no command; Extra is an argument past the last one the command takes.
enum class CommandStop : uint8 {
	None,
	Unknown,
	Extra
};


// What a line is at its end. Empty is a line with no words; Unknown and Fault are errors with the event filled in; Run is a command to
// call with the values in Values().
enum class CommandEnd : uint8 {
	Empty,
	Unknown,
	Fault,
	Run
};


// What the end of the line completes, for Completion. None is nothing. Name is a command or group name at the level, and Value is an enum
// value. For both, root is the tree the token reads and node is the node the token typed so far stands at (the root when nothing is typed).
// A Value typed in an argument's text has no node of its own here: slot is that argument's reader, and command and argument name it, so
// Completion reads the node through CommandSet::EnumNode. slot is null when node is already the answer.
enum class CommandSpot : uint8 {
	None,
	Name,
	Value
};


struct CommandEditPoint {
	CommandSpot spot = CommandSpot::None;
	uint16 root = 0;
	uint16 node = 0;
	const CommandSlot* slot = nullptr;
	std::size_t command = 0;
	std::size_t argument = 0;
};


// Calls a visitor with the typed set at a group path, for a set's VisitLevel. The visitor is a lambda with a template parameter.
template<typename Function>
class CommandLevelVisitor {
public:
	explicit CommandLevelVisitor(Function& callee) : function(callee) {
	}


	template<typename LevelSet>
	void operator()() {
		function.template operator()<LevelSet>();
	}


private:
	Function& function;
};


template<typename Set>
class CommandParser {
public:
	static constexpr std::size_t Levels = Set::GroupDepth + 1;
	static constexpr std::size_t NameDepth = CommandArraySize(Set::MaxNameLength);
	static constexpr std::size_t Arguments = CommandArraySize(Set::MaxArguments);
	static constexpr std::size_t SlotBytes = CommandArraySize(Set::MaxSlotBytes);
	static constexpr std::size_t ValueBytes = CommandArraySize(Set::MaxValueBytes);


private:
	std::array<uint16, Levels> groups{};
	std::array<std::size_t, Levels> nameTyped{};
	std::array<std::size_t, Levels> nameMatched{};
	std::array<std::size_t, Levels> nameGap{};
	std::array<std::array<uint16, NameDepth>, Levels> nameNodes{};
	std::array<std::array<char, CommandWordSize>, Levels> words{};
	std::array<std::size_t, Arguments> argTyped{};
	std::array<std::size_t, Arguments> argGap{};
	std::array<CommandSlot*, Arguments> slots{};
	alignas(Set::MaxSlotAlign) std::byte slotStorage[Arguments][SlotBytes]{};
	alignas(Set::MaxValueAlign) std::byte values[ValueBytes]{};
	std::size_t level = 0;
	std::size_t tail = 0;
	std::size_t spaces = 0;
	std::size_t command = 0;
	std::size_t argument = 0;
	CommandPhase phase = CommandPhase::Gap;
	CommandAfter after = CommandAfter::Start;
	CommandStop stop = CommandStop::None;
	std::size_t wordLength = 0;
	std::size_t wordGap = 0;
	bool committed = false;
	bool secretTyped = false;
	bool helpListed = false;


public:
	// Tells the parser that the Help module is listed. A line that starts with help is then a help request, and it is not hidden after an
	// unknown name, because its words are not secrets.
	void AllowHelp() {
		helpListed = true;
	}


	// Forgets the line: the next character starts a new one.
	void Reset() {
		groups.fill(0);
		nameTyped.fill(0);
		nameMatched.fill(0);
		nameGap.fill(0);
		argTyped.fill(0);
		argGap.fill(0);
		slots.fill(nullptr);
		level = 0;
		tail = 0;
		spaces = 0;
		command = 0;
		argument = 0;
		phase = CommandPhase::Gap;
		after = CommandAfter::Start;
		stop = CommandStop::None;
		wordLength = 0;
		wordGap = 0;
		committed = false;
		secretTyped = false;
	}


	// Reads one character of the line.
	void Push(char character) {
		if constexpr (Set::EntryCount == 0) {
			PushWord(character);
		} else {
			WithLevel([&]<typename L>() {
				PushCommandIn<L>(character);
			});
		}
	}


	// Removes the last character. Returns false when there is nothing to remove.
	bool Backspace() {
		if constexpr (Set::EntryCount == 0) {
			return BackspaceWord();
		} else {
			return BackspaceCommand();
		}
	}


	// Command mode. Ends the line: fills event for an error, and tells whether the command is to run.
	CommandEnd Evaluate(CommandEvent& event) {
		CommandEnd result = CommandEnd::Empty;
		WithLevel([&]<typename L>() {
			result = EvaluateIn<L>(event);
		});
		return result;
	}


	// Calls function with the typed set at the current group path, as CommandSet::VisitLevel does.
	template<typename Function>
	void WithLevel(Function&& function) const {
		CommandLevelVisitor<std::remove_reference_t<Function>> visitor(function);
		Set::VisitLevel(groups.data(), level, visitor);
	}


	// The token the end of the line is in, for Completion. Nothing is completed in a refused or stopped line.
	CommandEditPoint EditPoint() const {
		CommandEditPoint point;

		WithLevel([&]<typename L>() {
			point = EditPointIn<L>();
		});

		return point;
	}


	// The command the line names, once it names one. The values of its arguments are in Values().
	std::size_t Command() const {
		return command;
	}


	const std::byte* Values() const {
		return values;
	}


	// Word mode. A character that is not a space ends the current word when it comes after one: the word is then final, and its Word
	// event is raised before the character is read.
	bool WordEnds(char character) const {
		return character != ' ' && wordLength > 0 && spaces > 0;
	}


	// Whether the character, read now, goes to a Secret argument. A space never does: it is echoed as it is.
	bool HidesNext(char character) const {
		if constexpr (Set::EntryCount == 0) {
			static_cast<void>(character);
			return false;
		} else {
			bool hidden = false;

			WithLevel([&]<typename L>() {
				hidden = HiddenIn<L>(character);
			});

			return hidden;
		}
	}


	// Whether a Secret argument has had a character typed on this line. Erasing it does not clear this: the line is then not kept.
	bool HoldsSecret() const {
		return secretTyped;
	}


	bool HasWord() const {
		return committed || wordLength > 0;
	}


	std::size_t WordLength() const {
		return wordLength;
	}


	std::string_view WordText() const {
		return WordAt(0, wordLength);
	}


	// True when the word is longer than the word buffer, so WordText holds only its first characters.
	bool WordTruncated() const {
		return wordLength > CommandWordSize;
	}


private:
	std::string_view WordAt(std::size_t index, std::size_t typed) const {
		return std::string_view(words[index].data(), std::min(typed, CommandWordSize));
	}


	void StoreWord(std::size_t index, std::size_t position, char character) {
		if (position < CommandWordSize) {
			words[index][position] = character;
		}
	}


	void Stop(CommandStop reason) {
		stop = reason;
		tail = 1;
	}


	template<typename L>
	void PushSlot(std::size_t position, char character) {
		if (L::ArgumentInfoOf(command, position).kind == CommandArgumentKind::Secret) {
			secretTyped = true;
		}

		slots[position]->Push(character);
		argTyped[position]++;
	}


	// After a name that matches no command, the rest of the line is hidden, because the parser cannot tell which argument it would be. The
	// line is still a help request when Help is listed and the name is help, so nothing is hidden there.
	bool HidesAfterUnknown() const {
		if constexpr (Set::HasSecret) {
			return !(helpListed && level == 0 && WordAt(0, nameTyped[0]) == "help");
		} else {
			return false;
		}
	}


	// The character goes to a Secret argument: the argument being read, or the one a character starts. Inside the quotes of a secret every
	// character is hidden, the space too. A space that ends the argument, or a space in the gap, is echoed as it is.
	template<typename L>
	bool HiddenIn(char character) const {
		if (stop == CommandStop::Unknown) {
			return HidesAfterUnknown();
		}

		if (stop == CommandStop::Extra) {
			return character != ' ' && HidesAfterExtra<L>();
		}

		if (stop != CommandStop::None) {
			return false;
		}

		if (phase == CommandPhase::Argument) {
			if (L::ArgumentInfoOf(command, argument).kind != CommandArgumentKind::Secret) {
				return false;
			}

			return character != ' ' || slots[argument]->Quoting();
		}

		if (character == ' ') {
			return false;
		}

		if (phase == CommandPhase::Gap && (after == CommandAfter::Command || after == CommandAfter::Argument)) {
			if (argument >= L::ArgumentCountOf(command)) {
				return HidesAfterExtra<L>();
			}

			return L::ArgumentInfoOf(command, argument).kind == CommandArgumentKind::Secret;
		}

		return false;
	}


	// After the last argument of a command that has a Secret, the text past it is hidden too. It may be the rest of a password typed with a
	// space in it, and the parser cannot tell it from a mistake. A command with no Secret shows it as typed.
	template<typename L>
	bool HidesAfterExtra() const {
		if constexpr (Set::HasSecret) {
			for (std::size_t index = 0; index < L::ArgumentCountOf(command); index++) {
				if (L::ArgumentInfoOf(command, index).kind == CommandArgumentKind::Secret) {
					return true;
				}
			}
		}

		return false;
	}


	// Word mode, as described for WordEnds. A word that starts after spaces keeps their count in wordGap.
	void PushWord(char character) {
		if (character == ' ') {
			spaces++;
			return;
		}

		if (wordLength > 0 && spaces > 0) {
			committed = true;
			wordLength = 0;
		}

		if (wordLength == 0) {
			wordGap = spaces;
			spaces = 0;
		}

		StoreWord(0, wordLength, character);
		wordLength++;
	}


	// Word mode. The spaces after a committed word stay, but the last one before the current word is not removed: it keeps the two
	// words apart, so the line never shows a word glued to the one before it.
	bool BackspaceWord() {
		if (spaces > 0) {
			if (committed && wordLength == 0 && spaces == 1) {
				return false;
			}

			spaces--;
			return true;
		}

		if (wordLength == 0) {
			return false;
		}

		wordLength--;
		if (wordLength == 0) {
			spaces = wordGap;
		}

		return true;
	}


	// The token the end of the line is in, read at the level L. A name is read at the level's tree. A gap after a command or an argument
	// reads the next argument, when it is an enum, at the enum's tree.
	template<typename L>
	CommandEditPoint EditPointIn() const {
		CommandEditPoint point;

		if (stop != CommandStop::None) {
			return point;
		}

		if (phase == CommandPhase::Name) {
			if (nameTyped[level] > nameMatched[level]) {
				return point;
			}

			point.spot = CommandSpot::Name;
			point.root = L::Root;
			point.node = nameMatched[level] == 0 ? L::Root : nameNodes[level][nameMatched[level] - 1];
			return point;
		}

		if (phase == CommandPhase::Argument) {
			const CommandArgumentInfo& typing = L::ArgumentInfoOf(command, argument);
			if (typing.kind != CommandArgumentKind::Enumeration) {
				return point;
			}

			point.spot = CommandSpot::Value;
			point.root = typing.root;
			point.slot = slots[argument];
			point.command = command;
			point.argument = argument;
			return point;
		}

		if (after == CommandAfter::Start || after == CommandAfter::Group) {
			point.spot = CommandSpot::Name;
			point.root = L::Root;
			point.node = L::Root;
			return point;
		}

		if (argument >= L::ArgumentCountOf(command)) {
			return point;
		}

		const CommandArgumentInfo& info = L::ArgumentInfoOf(command, argument);
		if (info.kind != CommandArgumentKind::Enumeration) {
			return point;
		}

		point.spot = CommandSpot::Value;
		point.root = info.root;
		point.node = info.root;
		return point;
	}


	// Command mode, one character.
	template<typename L>
	void PushCommandIn(char character) {
		if (stop != CommandStop::None) {
			// What is hidden after an unknown name or past the last argument is a secret for the echo and for History, as HiddenIn decides.
			if (HiddenIn<L>(character)) {
				secretTyped = true;
			}

			tail++;
			return;
		}

		if (character == ' ') {
			PushSpace<L>();
			return;
		}

		switch (phase) {
			case CommandPhase::Name:
				PushNameCharacter<L>(character);
				return;
			case CommandPhase::Argument:
				PushSlot<L>(argument, character);
				return;
			case CommandPhase::Gap:
				if (after == CommandAfter::Start || after == CommandAfter::Group) {
					StartName<L>(character);
				} else {
					StartArgument<L>(character);
				}

				return;
		}
	}


	template<typename L>
	void PushSpace() {
		switch (phase) {
			case CommandPhase::Name:
				CloseName<L>();
				return;
			case CommandPhase::Argument:
				if (slots[argument]->Quoting()) {
					PushSlot<L>(argument, ' ');
				} else {
					argument++;
					phase = CommandPhase::Gap;
					spaces = 1;
					after = CommandAfter::Argument;
				}

				return;
			case CommandPhase::Gap:
				spaces++;
				return;
		}
	}


	// A name is read on its own tree: a character that matches moves to a child, and one that does not is counted but not matched. The
	// row of the level keeps the node after each matched character.
	template<typename L>
	void PushNameCharacter(char character) {
		bool onTree = nameMatched[level] == nameTyped[level];
		nameTyped[level]++;

		if (onTree) {
			uint16 node = nameMatched[level] == 0 ? L::Root : nameNodes[level][nameMatched[level] - 1];
			uint16 child = L::TreeStep(node, character);

			if (child != CommandNoNode) {
				nameNodes[level][nameMatched[level]] = child;
				nameMatched[level]++;
			}
		}

		StoreWord(level, nameTyped[level] - 1, character);
	}


	template<typename L>
	void StartName(char character) {
		nameGap[level] = spaces;
		spaces = 0;
		phase = CommandPhase::Name;
		nameTyped[level] = 0;
		nameMatched[level] = 0;
		PushNameCharacter<L>(character);
	}


	// A space after a name: a command starts its arguments, a group opens its level, and an unknown name stops the parse.
	template<typename L>
	void CloseName() {
		if (nameTyped[level] > nameMatched[level]) {
			Stop(CommandStop::Unknown);
			return;
		}

		uint16 entry = L::TreePayload(nameNodes[level][nameMatched[level] - 1]);
		if (entry == CommandNoNode) {
			Stop(CommandStop::Unknown);
			return;
		}

		const CommandEntryInfo& info = L::EntryInfo(entry);
		if (info.isGroup) {
			groups[level] = info.index;
			level++;
			after = CommandAfter::Group;
		} else {
			command = info.index;
			argument = 0;
			after = CommandAfter::Command;
		}

		phase = CommandPhase::Gap;
		spaces = 1;
	}


	template<typename L>
	void StartArgument(char character) {
		if (argument >= L::ArgumentCountOf(command)) {
			Stop(CommandStop::Extra);

			if (HidesAfterExtra<L>()) {
				secretTyped = true;
			}

			return;
		}

		argGap[argument] = spaces;
		spaces = 0;
		argTyped[argument] = 0;
		slots[argument] = L::CreateSlot(command, argument, slotStorage[argument]);
		phase = CommandPhase::Argument;
		PushSlot<L>(argument, character);
	}


	// Command mode, one Backspace. A token that becomes empty gives back the gap before it, and the gap gives back the token before it
	// when its last space goes.
	bool BackspaceCommand() {
		if (stop != CommandStop::None) {
			tail--;
			if (tail == 0) {
				stop = CommandStop::None;
			}

			return true;
		}

		switch (phase) {
			case CommandPhase::Name:
				if (nameTyped[level] > nameMatched[level]) {
					nameTyped[level]--;
				} else {
					nameTyped[level]--;
					nameMatched[level]--;
				}

				if (nameTyped[level] == 0) {
					phase = CommandPhase::Gap;
					spaces = nameGap[level];
					after = level == 0 ? CommandAfter::Start : CommandAfter::Group;
				}

				return true;
			case CommandPhase::Argument:
				slots[argument]->Pop();
				argTyped[argument]--;

				if (argTyped[argument] == 0) {
					phase = CommandPhase::Gap;
					spaces = argGap[argument];
					after = argument == 0 ? CommandAfter::Command : CommandAfter::Argument;
				}

				return true;
			case CommandPhase::Gap:
				if (spaces == 0) {
					return false;
				}

				spaces--;
				if (spaces == 0) {
					ReopenBeforeGap();
				}

				return true;
		}

		return false;
	}


	// The gap is empty: the token before it is open again, at its end.
	void ReopenBeforeGap() {
		switch (after) {
			case CommandAfter::Start:
				return;
			case CommandAfter::Command:
				phase = CommandPhase::Name;
				return;
			case CommandAfter::Group:
				level--;
				phase = CommandPhase::Name;
				return;
			case CommandAfter::Argument:
				argument--;
				phase = CommandPhase::Argument;
				return;
		}
	}


	template<typename L>
	CommandEnd EvaluateIn(CommandEvent& event) {
		if (stop == CommandStop::Unknown) {
			return UnknownWord(event, level);
		}

		if (stop == CommandStop::Extra) {
			FillCommand<L>(event);
			event.kind = CommandEventKind::ArgumentError;
			event.fault = CommandFault::Extra;
			return CommandEnd::Fault;
		}

		std::size_t started = 0;

		switch (phase) {
			case CommandPhase::Name: {
				if (nameTyped[level] > nameMatched[level]) {
					return UnknownWord(event, level);
				}

				uint16 entry = L::TreePayload(nameNodes[level][nameMatched[level] - 1]);
				if (entry == CommandNoNode) {
					return UnknownWord(event, level);
				}

				const CommandEntryInfo& info = L::EntryInfo(entry);
				if (info.isGroup) {
					return UnknownWord(event, level);
				}

				command = info.index;
				argument = 0;
				break;
			}
			case CommandPhase::Gap:
				if (after == CommandAfter::Start) {
					event.kind = CommandEventKind::EmptyLine;
					return CommandEnd::Empty;
				}

				if (after == CommandAfter::Group) {
					return UnknownWord(event, GroupWordLevel());
				}

				started = argument;
				break;
			case CommandPhase::Argument:
				started = argument + 1;
				break;
		}

		return RunCommand<L>(event, started);
	}


	// The level of the group whose name is the last word. A group gap is always past level 0, and the bound keeps the index in range.
	std::size_t GroupWordLevel() const {
		return level > 0 ? level - 1 : 0;
	}


	CommandEnd UnknownWord(CommandEvent& event, std::size_t wordLevel) const {
		event.kind = CommandEventKind::UnknownCommand;
		event.text = WordAt(wordLevel, nameTyped[wordLevel]);
		return CommandEnd::Unknown;
	}


	template<typename L>
	void FillCommand(CommandEvent& event) const {
		event.command = L::CommandName(command);
	}


	// A float argument's range is not in its info, so the event reads it from the declaration. Only a set with float arguments does this.
	// It writes the float range, which shares storage with the integer range, so FillArgument does not write the integer one for a float.
	template<typename L>
	void FillFloatRange(CommandEvent& event, std::size_t position) const {
		if constexpr (Set::HasFloat) {
			L::VisitArgument(command, position, [&]<typename Declaration>(const Declaration& declaration) {
				if constexpr (Declaration::Kind == CommandArgumentKind::Float) {
					event.range.floats.minimum = static_cast<double>(declaration.minimum);
					event.range.floats.maximum = static_cast<double>(declaration.maximum);
				}
			});
		} else {
			static_cast<void>(event);
			static_cast<void>(position);
		}
	}


	static void FillArgument(CommandEvent& event, const CommandArgumentInfo& info, CommandFault fault) {
		event.kind = CommandEventKind::ArgumentError;
		event.fault = fault;
		event.argument = info.name;
		event.argumentKind = info.kind;

		if (info.kind != CommandArgumentKind::Float) {
			event.range.integers.minimum = info.minimum;
			event.range.integers.maximum = info.maximum;
			event.range.integers.maximumUnsigned = info.maximumUnsigned;
			event.range.integers.unsignedMaximum = info.unsignedMaximum;
		}
	}


	// Reads every argument of the command in order. The first one that is refused, or missing without a default, ends the check. The
	// started arguments are the ones with a reader; the rest take their default, or are missing.
	template<typename L>
	CommandEnd RunCommand(CommandEvent& event, std::size_t started) {
		FillCommand<L>(event);

		std::size_t count = L::ArgumentCountOf(command);
		for (std::size_t position = 0; position < count; position++) {
			const CommandArgumentInfo& info = L::ArgumentInfoOf(command, position);

			if (position < started) {
				CommandFault fault = slots[position]->Finish(values + info.valueOffset);
				if (fault != CommandFault::None) {
					FillArgument(event, info, fault);
					FillFloatRange<L>(event, position);
					return CommandEnd::Fault;
				}
			} else if (info.hasDefault) {
				L::StoreDefault(command, position, values);
			} else {
				FillArgument(event, info, CommandFault::Missing);
				FillFloatRange<L>(event, position);
				return CommandEnd::Fault;
			}
		}

		return CommandEnd::Run;
	}
};
