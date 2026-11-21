#pragma once
#include "CommandArgument.h"
#include "CommandCheck.h"
#include "CommandNameTree.h"
#include "CommandPack.h"
#include "CommandSetBuilder.h"
#include "CommandTypes.h"
#include <Utilities/DataTypes.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>
#include <utility>


// The tables of one level of a command set: the name tree, the entries (commands and groups in declaration order), the commands and
// the arguments. A level is built in arrays of the maximum size, then copied to arrays of exact size, so flash holds only what the
// set uses. A level with no commands and no groups has no tree at all.


// The largest level the builder accepts. These bound the scratch arrays of the build, not the flash of a set.
constexpr std::size_t CommandMaxNodes = 1024;
constexpr std::size_t CommandMaxArguments = 256;
constexpr std::size_t CommandMaxEntries = 256;


// One command or group of a level. index is the command's or the group's index among commands, or among groups.
struct CommandEntryInfo {
	std::string_view name;
	std::string_view help;
	uint16 index = 0;
	bool isGroup = false;
};


// One command: where its arguments start in the level's argument list, how many it has, the size of its value area and its entry.
struct CommandRecord {
	uint16 firstArgument = 0;
	uint16 argumentCount = 0;
	uint16 valueBytes = 0;
	uint16 entry = 0;
};


template<std::size_t Nodes, std::size_t Arguments, std::size_t Entries, std::size_t Commands>
struct CommandLevel {
	std::array<CommandTreeNode, Nodes> nodes{};
	std::array<CommandArgumentInfo, Arguments> arguments{};
	std::array<CommandEntryInfo, Entries> entries{};
	std::array<CommandRecord, Commands> commands{};
};


// How many of each table a level has. The exact arrays take these sizes.
struct CommandLevelCounts {
	std::size_t nodes = 0;
	std::size_t arguments = 0;
	std::size_t entries = 0;
	std::size_t commands = 0;
};


// The largest scratch level, in which the build runs before the copy.
using CommandScratch = CommandLevel<CommandMaxNodes, CommandMaxArguments, CommandMaxEntries, CommandMaxEntries>;


// The state of a build: the scratch level, the tree builder of the level's names, and the counts so far.
struct CommandBuild {
	CommandScratch level{};
	CommandTreeBuilder<CommandMaxNodes> tree{};
	uint16 root = 0;
	std::size_t entry = 0;
	std::size_t command = 0;
	std::size_t group = 0;
	std::size_t argument = 0;
};


template<typename Declaration>
constexpr void CommandAddArgument(const Declaration& declaration, CommandBuild& build, std::size_t& offset) {
	using Value = typename Declaration::Value;
	using Slot = typename CommandSlotOf<Declaration>::Type;

	CommandRequireName(declaration.name);
	declaration.Verify();
	CommandRequire<CommandText{"the command set has too many arguments for the builder"}>(build.argument < CommandMaxArguments);

	CommandArgumentInfo info = CommandDescribe(declaration);

	offset = CommandAlignUp(offset, alignof(Value));
	info.valueOffset = static_cast<uint16>(offset);
	info.valueAlign = static_cast<uint8>(alignof(Value));
	offset += sizeof(Value);

	info.slotBytes = static_cast<uint16>(sizeof(Slot));
	info.slotAlign = static_cast<uint8>(alignof(Slot));

	if constexpr (Declaration::Kind == CommandArgumentKind::Enumeration) {
		info.root = build.tree.AddRoot();

		for (std::size_t index = 0; index < declaration.values.size(); index++) {
			CommandRequire<CommandText{"duplicate enum names"}>(build.tree.Insert(info.root, declaration.values[index].name, static_cast<uint16>(index)));
		}
	}

	build.level.arguments[build.argument] = info;
	build.argument++;
}


template<typename Handler, typename... Arguments>
constexpr void CommandAddItem(const CommandItem<Handler, Arguments...>& item, CommandBuild& build) {
	CommandRequire<CommandText{"the command set has too many commands and groups for the builder"}>(build.entry < CommandMaxEntries);
	CommandRequire<CommandText{"the command set has too many arguments for the builder"}>(build.argument + sizeof...(Arguments) <= CommandMaxArguments);
	CommandRequire<CommandText{"duplicate names at one level"}>(build.tree.Insert(build.root, item.name, static_cast<uint16>(build.entry)));

	CommandRecord record;
	record.firstArgument = static_cast<uint16>(build.argument);
	record.argumentCount = static_cast<uint16>(sizeof...(Arguments));
	record.entry = static_cast<uint16>(build.entry);

	std::size_t offset = 0;
	CommandPackForEach(item.arguments, [&](const auto& argument) {
		CommandAddArgument(argument, build, offset);
	});

	record.valueBytes = static_cast<uint16>(offset);
	build.level.commands[build.command] = record;
	build.level.entries[build.entry] = CommandEntryInfo{item.name, item.help, static_cast<uint16>(build.command), false};
	build.command++;
	build.entry++;
}


template<typename Set>
constexpr void CommandAddItem(const GroupItem<Set>& item, CommandBuild& build) {
	CommandRequire<CommandText{"the command set has too many commands and groups for the builder"}>(build.entry < CommandMaxEntries);
	CommandRequire<CommandText{"duplicate names at one level"}>(build.tree.Insert(build.root, item.name, static_cast<uint16>(build.entry)));

	build.level.entries[build.entry] = CommandEntryInfo{item.name, item.help, static_cast<uint16>(build.group), true};
	build.group++;
	build.entry++;
}


// Default and interrupt handlers are not entries of a level, so they add nothing to its tables.
template<typename Handler>
constexpr void CommandAddItem(const DefaultItem<Handler>&, CommandBuild&) {
}


template<typename Handler>
constexpr void CommandAddItem(const InterruptItem<Handler>&, CommandBuild&) {
}


// Builds every table of a level in the scratch arrays.
template<typename... Items>
constexpr CommandBuild CommandBuildScratch(const CommandSetBuilder<Items...>& builder) {
	CommandBuild build;
	build.root = build.tree.AddRoot();

	CommandPackForEach(builder.items, [&](const auto& item) {
		CommandAddItem(item, build);
	});

	return build;
}


// The table sizes of a builder. The name tree of a level with no entries is empty.
template<typename... Items>
constexpr CommandLevelCounts CommandMeasure(const CommandSetBuilder<Items...>& builder) {
	CommandBuild build = CommandBuildScratch(builder);

	CommandLevelCounts counts;
	counts.entries = build.entry;
	counts.commands = build.command;
	counts.arguments = build.argument;
	counts.nodes = build.entry == 0 ? 0 : build.tree.count;
	return counts;
}


// Builds a level with exact table sizes. The sizes come from CommandMeasure of the same builder.
template<std::size_t Nodes, std::size_t Arguments, std::size_t Entries, std::size_t Commands, typename... Items>
constexpr CommandLevel<Nodes, Arguments, Entries, Commands> CommandBuildLevel(const CommandSetBuilder<Items...>& builder) {
	CommandBuild build = CommandBuildScratch(builder);
	CommandLevel<Nodes, Arguments, Entries, Commands> level;

	for (std::size_t index = 0; index < Nodes; index++) {
		level.nodes[index] = build.tree.nodes[index];
	}

	for (std::size_t index = 0; index < Arguments; index++) {
		level.arguments[index] = build.level.arguments[index];
	}

	for (std::size_t index = 0; index < Entries; index++) {
		level.entries[index] = build.level.entries[index];
	}

	for (std::size_t index = 0; index < Commands; index++) {
		level.commands[index] = build.level.commands[index];
	}

	return level;
}


// The sizes a command set needs at run time. The maximums cover the set and every group under it, because a group's commands run
// in the same console and share its value area and slots.
struct CommandLimits {
	std::size_t arguments = 0;
	std::size_t valueBytes = 0;
	std::size_t valueAlign = 1;
	std::size_t slotBytes = 0;
	std::size_t slotAlign = 1;
	std::size_t groupDepth = 0;
	std::size_t nameLength = 0;
	std::size_t stepBytes = 0;
	std::size_t stepAlign = 1;
	bool hasLong = false;
	bool hasFloat = false;
	bool hasSecret = false;
};


constexpr CommandLimits CommandMerge(const CommandLimits& first, const CommandLimits& second) {
	CommandLimits result;
	result.arguments = std::max(first.arguments, second.arguments);
	result.valueBytes = std::max(first.valueBytes, second.valueBytes);
	result.valueAlign = std::max(first.valueAlign, second.valueAlign);
	result.slotBytes = std::max(first.slotBytes, second.slotBytes);
	result.slotAlign = std::max(first.slotAlign, second.slotAlign);
	result.groupDepth = std::max(first.groupDepth, second.groupDepth);
	result.nameLength = std::max(first.nameLength, second.nameLength);
	result.stepBytes = std::max(first.stepBytes, second.stepBytes);
	result.stepAlign = std::max(first.stepAlign, second.stepAlign);
	result.hasLong = first.hasLong || second.hasLong;
	result.hasFloat = first.hasFloat || second.hasFloat;
	result.hasSecret = first.hasSecret || second.hasSecret;
	return result;
}


// The limits of the commands of one level, without the groups under it.
template<typename Level>
constexpr CommandLimits CommandOwnLimits(const Level& level) {
	CommandLimits limits;

	for (const CommandRecord& record : level.commands) {
		limits.arguments = std::max<std::size_t>(limits.arguments, record.argumentCount);
		limits.valueBytes = std::max<std::size_t>(limits.valueBytes, record.valueBytes);
	}

	for (const CommandArgumentInfo& info : level.arguments) {
		limits.valueAlign = std::max<std::size_t>(limits.valueAlign, info.valueAlign);
		limits.slotBytes = std::max<std::size_t>(limits.slotBytes, info.slotBytes);
		limits.slotAlign = std::max<std::size_t>(limits.slotAlign, info.slotAlign);
		limits.hasFloat = limits.hasFloat || info.kind == CommandArgumentKind::Float;
		limits.hasSecret = limits.hasSecret || info.kind == CommandArgumentKind::Secret;
	}

	for (const CommandEntryInfo& entry : level.entries) {
		limits.nameLength = std::max<std::size_t>(limits.nameLength, entry.name.size());
	}

	return limits;
}


// The limits a group adds to the set above it: its own, and one more level of nesting.
constexpr CommandLimits CommandNested(const CommandLimits& child) {
	CommandLimits limits = child;
	limits.groupDepth = child.groupDepth + 1;
	return limits;
}


// The limits an item adds to its level. A command adds nothing beyond the level's own tables. A group adds its set's limits.
template<typename Item>
struct CommandChildLimits {
	static constexpr CommandLimits Value{};
};


template<typename Set>
struct CommandChildLimits<GroupItem<Set>> {
	static constexpr CommandLimits Value = CommandNested(Set::Limits);
};


// The limits a command's handler adds: a long command's state, which the console keeps while the command runs. Other items add nothing.
template<typename Item>
constexpr CommandLimits CommandItemLimits(const Item&) {
	return CommandLimits{};
}


template<typename Handler, typename... Arguments>
constexpr CommandLimits CommandItemLimits(const CommandItem<Handler, Arguments...>&) {
	CommandLimits limits;

	if constexpr (CommandHandlerOf<Handler>::Long) {
		using State = typename CommandHandlerOf<Handler>::State;
		limits.hasLong = true;
		limits.stepBytes = sizeof(State);
		limits.stepAlign = alignof(State);
	}

	return limits;
}


template<typename Level, typename... Items>
constexpr CommandLimits CommandLimitsOf(const Level& level, const CommandSetBuilder<Items...>& builder) {
	CommandLimits limits = CommandOwnLimits(level);
	((limits = CommandMerge(limits, CommandChildLimits<Items>::Value)), ...);
	CommandPackForEach(builder.items, [&](const auto& item) {
		limits = CommandMerge(limits, CommandItemLimits(item));
	});
	return limits;
}
