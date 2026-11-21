#pragma once
#include "CommandArgument.h"
#include "CommandCheck.h"
#include "CommandLevel.h"
#include "CommandNameTree.h"
#include "CommandPack.h"
#include "CommandSetBuilder.h"
#include "CommandTypes.h"
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <cstddef>
#include <cstring>
#include <new>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>


// A command set built at compile time from a builder chain (CommandSetBuilder.h). The set holds the tables of its level in flash and
// gives the runtime the operations it needs. A group is another CommandSet, so a set's level is found by walking a path of group
// indexes, and VisitLevel gives the typed set at the end of the path.
//
//   using AppCommands = CommandSet<+[] {
//       return CommandSetBuilder()
//           .Command("key", "press a key", &Key, Argument<uint8>("code"))
//           .Default(&OnEvent);
//   }>;
//
// The operations a console uses, at the level it is on:
//   Root                          node index of the root of a level's name tree
//   TreeStep(node, c)             the node that c leads to from node, or CommandNoNode (the level must have entries)
//   TreePayload(node)             the entry of a complete name, or the value index of a complete enum value
//   TreeFirstChild, TreeSibling, TreeCharacter(node)   the children and the character of a node, for walking the tree
//   EntryInfo(entry)              name, help, and whether the entry is a group, with its index
//   CommandName(command)          the name of a command
//   ArgumentCountOf(command)      how many arguments a command has
//   ArgumentInfoOf(command, p)    name, kind, range, default flag, value offset, and slot size of argument p
//   CreateSlot(command, p, mem)   placement-creates the reader of argument p in mem (MaxSlotBytes, aligned to MaxSlotAlign)
//   EnumNode(command, p, slot, n) the node an enum reader stands at, for Completion (the slot's type comes from the declaration)
//   StoreDefault(command, p, v)   writes the default of argument p into the value area v
//   Invoke(command, app, out, v)  calls the command's handler with the values in the value area v, and returns its status
//   CallDefault(app, out, event)  calls the default handler (when HasDefault)
//   CallInterrupt(app)            calls the interrupt handler (when HasInterrupt)
//   CheckContext<App>()           a compile-time check that App is, or derives from, the context of every handler
//   VisitLevel(groups, n, v)      calls v.template operator()<Set>() with the typed set at the group path of n indexes
//   VisitCommand(command, v)      calls v(item) with the typed item of a command of this level (for its declared arguments)
//
// Each command's values sit in the value area at the offsets ArgumentInfoOf gives. The area holds MaxValueBytes, aligned to
// MaxValueAlign, for the largest command of this set or of any group under it.


// Whether a builder holds a Default() item, and whether it holds an OnInterrupt() item.
template<typename... Items>
constexpr bool CommandAnyDefault(const CommandSetBuilder<Items...>&) {
	return (CommandIsDefault<Items> || ...);
}


template<typename... Items>
constexpr bool CommandAnyInterrupt(const CommandSetBuilder<Items...>&) {
	return (CommandIsInterrupt<Items> || ...);
}


// Reads one value of type Value from the value area at offset. The area is copied, so an unaligned value is read safely.
template<typename Value>
Value CommandReadValue(const std::byte* values, std::size_t offset) {
	Value value{};
	std::memcpy(&value, values + offset, sizeof(Value));
	return value;
}


// Calls a handler with the parameters read from the value area. The handler's parameter types are the argument types.
template<typename Handler, typename Level, typename Call, std::size_t... Position>
ResultStatus CommandInvokeHandler(
	Handler handler,
	const Level& level,
	std::size_t first,
	Call call,
	const std::byte* values,
	std::index_sequence<Position...>)
{
	static_cast<void>(level);
	static_cast<void>(first);
	static_cast<void>(values);
	return handler(
		call,
		CommandReadValue<std::tuple_element_t<Position, typename CommandHandlerOf<Handler>::Values>>(values, level.arguments[first + Position].valueOffset)...
	);
}


template<typename Level, typename App, typename Handler, typename... Arguments>
ResultStatus CommandRun(
	const CommandItem<Handler, Arguments...>& item,
	const Level& level,
	std::size_t first,
	App& app,
	Print& out,
	const std::byte* values)
{
	using Context = typename CommandHandlerOf<Handler>::Ctx;
	CommandCall<Context> call{out, app};
	return CommandInvokeHandler(item.handler, level, first, call, values, std::index_sequence_for<Arguments...>{});
}


// Calls a handler that returns a step, with the parameters read from the value area. An ordinary handler's status converts to a final step.
template<typename Handler, typename Level, typename Call, std::size_t... Position>
CommandStep CommandInvokeStep(
	Handler handler,
	const Level& level,
	std::size_t first,
	Call call,
	const std::byte* values,
	std::index_sequence<Position...>)
{
	static_cast<void>(level);
	static_cast<void>(first);
	static_cast<void>(values);
	return handler(
		call,
		CommandReadValue<std::tuple_element_t<Position, typename CommandHandlerOf<Handler>::Values>>(values, level.arguments[first + Position].valueOffset)...);
}


// Runs one step of a command. A long command's state is made at the start, in the console's step slot, and the handler gets it with the
// cancel flag. An ordinary command runs once, at the start, and its status is the step.
template<typename Level, typename App, typename Handler, typename... Arguments>
CommandStep CommandStepItem(
	const CommandItem<Handler, Arguments...>& item,
	const Level& level,
	std::size_t first,
	App& app,
	Print& out,
	const std::byte* values,
	void* state,
	CommandStepMode mode)
{
	if constexpr (CommandHandlerOf<Handler>::Long) {
		using Context = typename CommandHandlerOf<Handler>::Ctx;
		using State = typename CommandHandlerOf<Handler>::State;
		static_assert(std::is_trivially_destructible_v<State>, "a long command's state must be trivially destructible");

		if (mode == CommandStepMode::Start) {
			::new (state) State{};
		}

		State& stored = *static_cast<State*>(state);
		CommandLongCall<Context, State> call{out, app, stored, mode == CommandStepMode::Cancel};
		return CommandInvokeStep(item.handler, level, first, call, values, std::index_sequence_for<Arguments...>{});
	} else {
		using Context = typename CommandHandlerOf<Handler>::Ctx;

		static_cast<void>(state);
		static_cast<void>(mode);

		CommandCall<Context> call{out, app};
		return CommandInvokeStep(item.handler, level, first, call, values, std::index_sequence_for<Arguments...>{});
	}
}


// Visits the command with the given index. The visitor is called only for that item.
template<typename Visitor, typename Handler, typename... Arguments>
void CommandVisitCommand(
	const CommandItem<Handler, Arguments...>& item,
	std::size_t command,
	std::size_t& counter,
	Visitor& visitor)
{
	if (counter == command) {
		visitor(item);
	}

	counter++;
}


template<typename Visitor, typename Item>
void CommandVisitCommand(const Item&, std::size_t, std::size_t&, Visitor&) {
}


// Visits the argument at position of a command item.
template<typename Declaration, typename Visitor>
void CommandVisitArgument(const Declaration& declaration, std::size_t index, std::size_t position, Visitor& visitor) {
	if (index == position) {
		visitor(declaration);
	}
}


template<typename Visitor, typename Handler, typename... Arguments>
void CommandVisitArguments(const CommandItem<Handler, Arguments...>& item, std::size_t position, Visitor& visitor) {
	std::size_t index = 0;
	CommandPackForEach(item.arguments, [&](const auto& declaration) {
		CommandVisitArgument(declaration, index, position, visitor);
		index++;
	});
}


template<typename Visitor, typename Item>
void CommandVisitArguments(const Item&, std::size_t, Visitor&) {
}


// Creates the reader of a declaration in storage. An enum reader also gets the level's tree and its root.
template<typename Declaration>
CommandSlot* CommandMakeSlot(
	const Declaration& declaration,
	void* storage,
	std::span<const CommandTreeNode> nodes,
	uint16 root)
{
	using Slot = typename CommandSlotOf<Declaration>::Type;

	if constexpr (Declaration::Kind == CommandArgumentKind::Enumeration) {
		return ::new (storage) Slot(declaration, nodes, root);
	} else {
		static_cast<void>(nodes);
		static_cast<void>(root);
		return ::new (storage) Slot(declaration);
	}
}


template<typename App, typename Handler>
void CommandRunDefault(const DefaultItem<Handler>& item, App& app, Print& out, const CommandEvent& event) {
	using Context = typename CommandDefaultOf<Handler>::Ctx;
	item.handler(CommandCall<Context>{out, app}, event);
}


template<typename App, typename Item>
void CommandRunDefault(const Item&, App&, Print&, const CommandEvent&) {
}


template<typename App, typename Handler>
void CommandRunInterrupt(const InterruptItem<Handler>& item, App& app) {
	item.handler(app);
}


template<typename App, typename Item>
void CommandRunInterrupt(const Item&, App&) {
}


// The compile-time context check of one item. A group checks its own set in turn.
template<typename App, typename Handler, typename... Arguments>
void CommandCheckContext(const CommandItem<Handler, Arguments...>&) {
	using Context = typename CommandHandlerOf<Handler>::Ctx;
	static_assert(std::is_convertible_v<App&, Context&>, "the context of a handler is not the application type or one of its bases");
}


template<typename App, typename Handler>
void CommandCheckContext(const DefaultItem<Handler>&) {
	using Context = typename CommandDefaultOf<Handler>::Ctx;
	static_assert(std::is_convertible_v<App&, Context&>, "the context of the default handler is not the application type or one of its bases");
}


template<typename App, typename Handler>
void CommandCheckContext(const InterruptItem<Handler>&) {
	using Context = typename CommandInterruptOf<Handler>::Ctx;
	static_assert(std::is_convertible_v<App&, Context&>, "the context of the interrupt handler is not the application type or one of its bases");
}


template<typename App, typename Set>
void CommandCheckContext(const GroupItem<Set>&) {
	Set::template CheckContext<App>();
}


template<typename App, typename Item>
void CommandCheckContext(const Item&) {
}


// Visits the group at the position, when the path goes through it. position counts the groups seen so far.
template<typename Visitor, typename Set>
void CommandVisitChild(
	const GroupItem<Set>&,
	std::size_t target,
	std::size_t& position,
	const uint16* groups,
	std::size_t depth,
	Visitor& visitor)
{
	if (position == target) {
		Set::VisitLevel(groups, depth, visitor);
	}

	position++;
}


template<typename Visitor, typename Item>
void CommandVisitChild(const Item&, std::size_t, std::size_t&, const uint16*, std::size_t, Visitor&) {
}


template<auto (*Make)()>
class CommandSet {
public:
	using Builder = decltype(Make());
	static constexpr Builder Built = Make();
	static constexpr CommandLevelCounts Counts = CommandMeasure(Built);
	static constexpr CommandLevel<Counts.nodes, Counts.arguments, Counts.entries, Counts.commands> Level = CommandBuildLevel<Counts.nodes, Counts.arguments, Counts.entries, Counts.commands>(Built);
	static constexpr CommandLimits Limits = CommandLimitsOf(Level, Built);

	// The commands and groups of this level.
	static constexpr std::size_t CommandCount = Counts.commands;
	static constexpr std::size_t EntryCount = Counts.entries;
	static constexpr std::size_t GroupCount = Counts.entries - Counts.commands;
	static constexpr bool HasDefault = CommandAnyDefault(Built);
	static constexpr bool HasInterrupt = CommandAnyInterrupt(Built);

	// Which modules the set needs: a long command, a float argument and a secret argument (see their modules).
	static constexpr bool HasLong = Limits.hasLong;
	static constexpr bool HasFloat = Limits.hasFloat;
	static constexpr bool HasSecret = Limits.hasSecret;

	// The largest values over this set and every group under it. The runtime sizes its value area and slot storage with these.
	static constexpr std::size_t GroupDepth = Limits.groupDepth;
	static constexpr std::size_t MaxNameLength = Limits.nameLength;
	static constexpr std::size_t MaxArguments = Limits.arguments;
	static constexpr std::size_t MaxValueBytes = Limits.valueBytes;
	static constexpr std::size_t MaxValueAlign = Limits.valueAlign;
	static constexpr std::size_t MaxSlotAlign = Limits.slotAlign;

	// The largest state of a long command, and its alignment. The console's step slot holds one state at a time.
	static constexpr std::size_t MaxStepBytes = Limits.stepBytes;
	static constexpr std::size_t MaxStepAlign = Limits.stepAlign;

	// Rounded up to the alignment, so the readers of an array of slots each start aligned, on every target.
	static constexpr std::size_t MaxSlotBytes = CommandAlignUp(Limits.slotBytes, Limits.slotAlign);
	static_assert(MaxSlotBytes % MaxSlotAlign == 0, "a slot stride is not a multiple of the slot alignment");

	static constexpr uint16 Root = 0;


	// One step through the name tree of this level.
	static constexpr uint16 TreeStep(uint16 node, char character) {
		return CommandTreeStep(std::span<const CommandTreeNode>(Level.nodes), node, character);
	}


	static constexpr uint16 TreePayload(uint16 node) {
		return Level.nodes[node].payload;
	}


	// The first child and the next sibling of a node, and the character a node holds. Completion walks the tree with these.
	static constexpr uint16 TreeFirstChild(uint16 node) {
		return Level.nodes[node].firstChild;
	}


	static constexpr uint16 TreeSibling(uint16 node) {
		return Level.nodes[node].sibling;
	}


	static constexpr char TreeCharacter(uint16 node) {
		return Level.nodes[node].character;
	}


	static constexpr const CommandEntryInfo& EntryInfo(std::size_t entry) {
		return Level.entries[entry];
	}


	static constexpr std::string_view CommandName(std::size_t command) {
		return Level.entries[Level.commands[command].entry].name;
	}


	static constexpr std::size_t ArgumentCountOf(std::size_t command) {
		return Level.commands[command].argumentCount;
	}


	static constexpr const CommandArgumentInfo& ArgumentInfoOf(std::size_t command, std::size_t position) {
		return Level.arguments[Level.commands[command].firstArgument + position];
	}


	// Calls visitor.template operator()<Set>() with the typed set at the end of the group path. groups holds depth group indexes,
	// from this set down, and an empty path calls the visitor with this set.
	template<typename Visitor>
	static void VisitLevel(const uint16* groups, std::size_t depth, Visitor& visitor) {
		if (depth == 0) {
			visitor.template operator()<CommandSet>();
			return;
		}

		std::size_t position = 0;
		std::size_t target = groups[0];
		CommandPackForEach(Built.items, [&](const auto& item) {
			CommandVisitChild(item, target, position, groups + 1, depth - 1, visitor);
		});
	}


	// Calls visitor(item) with the typed item of a command of this level, so a caller can read its declared arguments.
	template<typename Visitor>
	static void VisitCommand(std::size_t command, Visitor& visitor) {
		ForCommand(command, [&](const auto& item) {
			visitor(item);
		});
	}


	// Calls the handler of a command of this level. values is the command's value area.
	template<typename App>
	static ResultStatus Invoke(std::size_t command, App& app, Print& out, const std::byte* values) {
		ResultStatus status = ResultStatus::notFound;

		ForCommand(command, [&](const auto& item) {
			status = CommandRun(item, Level, Level.commands[command].firstArgument, app, out, values);
		});

		return status;
	}


	// Runs one step of a command: the start, the next step, or the cancel call (see CommandStepMode). state is the console's step slot,
	// of MaxStepBytes aligned to MaxStepAlign. An ordinary command gives its status as a final step.
	template<typename App>
	static CommandStep Step(std::size_t command, App& app, Print& out, const std::byte* values, void* state, CommandStepMode mode) {
		CommandStep step = ResultStatus::notFound;

		ForCommand(command, [&](const auto& item) {
			step = CommandStepItem(item, Level, Level.commands[command].firstArgument, app, out, values, state, mode);
		});

		return step;
	}


	// Calls visitor with the typed declaration of argument position of a command. The events that report a float range need it.
	template<typename Visitor>
	static void VisitArgument(std::size_t command, std::size_t position, Visitor visitor) {
		ForArgument(command, position, visitor);
	}


	// Creates the reader of argument position of a command in storage, and returns it. Returns nullptr when there is no such argument.
	static CommandSlot* CreateSlot(std::size_t command, std::size_t position, void* storage) {
		CommandSlot* slot = nullptr;

		ForArgument(command, position, [&](const auto& declaration) {
			uint16 root = Level.arguments[Level.commands[command].firstArgument + position].root;
			slot = CommandMakeSlot(declaration, storage, std::span<const CommandTreeNode>(Level.nodes), root);
		});

		return slot;
	}


	// The node of an enum argument's tree that its reader stands at, for Completion. slot is the reader of argument position of a command.
	// The reader's type is the one the declaration gives, so the slot is read as that type. Returns false when the text is refused.
	static bool EnumNode(std::size_t command, std::size_t position, const CommandSlot* slot, uint16& node) {
		bool found = false;

		ForArgument(command, position, [&]<typename Declaration>(const Declaration& declaration) {
			static_cast<void>(declaration);

			if constexpr (Declaration::Kind == CommandArgumentKind::Enumeration) {
				using Reader = typename CommandSlotOf<Declaration>::Type;
				found = static_cast<const Reader*>(slot)->TreeNode(node);
			} else {
				static_cast<void>(slot);
				static_cast<void>(node);
			}
		});

		return found;
	}


	// Writes the default of an optional argument into the value area. Does nothing for a required argument.
	static void StoreDefault(std::size_t command, std::size_t position, std::byte* values) {
		ForArgument(command, position, [&](const auto& declaration) {
			if (declaration.hasDefault) {
				std::size_t offset = Level.arguments[Level.commands[command].firstArgument + position].valueOffset;
				std::memcpy(values + offset, &declaration.defaultValue, sizeof(declaration.defaultValue));
			}
		});
	}


	// Calls the default handler. Does nothing when the set has none.
	template<typename App>
	static void CallDefault(App& app, Print& out, const CommandEvent& event) {
		CommandPackForEach(Built.items, [&](const auto& item) {
			CommandRunDefault(item, app, out, event);
		});
	}


	// Calls the interrupt handler. Does nothing when the set has none.
	template<typename App>
	static void CallInterrupt(App& app) {
		CommandPackForEach(Built.items, [&](const auto& item) {
			CommandRunInterrupt(item, app);
		});
	}


	// Checks, at compile time, that App is or derives from the context of every handler in this set and in its groups.
	template<typename App>
	static void CheckContext() {
		CommandPackForEach(Built.items, [](const auto& item) {
			CommandCheckContext<App>(item);
		});
	}


private:
	template<typename Visitor>
	static void ForCommand(std::size_t command, Visitor visitor) {
		std::size_t counter = 0;
		CommandPackForEach(Built.items, [&](const auto& item) {
			CommandVisitCommand(item, command, counter, visitor);
		});
	}


	template<typename Visitor>
	static void ForArgument(std::size_t command, std::size_t position, Visitor visitor) {
		ForCommand(command, [&](const auto& item) {
			CommandVisitArguments(item, position, visitor);
		});
	}
};
