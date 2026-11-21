#pragma once
#include "CommandArgument.h"
#include "CommandCheck.h"
#include "CommandNameTree.h"
#include "CommandPack.h"
#include "CommandTypes.h"
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


// The compile-time builder of a command set. Each call returns a new builder type that holds one more item, so the handler of each
// command keeps its own function type, and nothing is stored through a cast.
//
//   using AppCommands = CommandSet<+[] {
//       return CommandSetBuilder()
//           .Command("key", "press a key", &Key, Argument<uint8>("code").Range(0, 255))
//           .Group<MotorCommands>("motor", "motor control")
//           .OnInterrupt(&StopMotor)
//           .Default(&OnEvent);
//   }>;
//
// The chain is a captureless lambda, as for the FAT12 image and the USB descriptors. CommandSet (CommandSet.h) evaluates it.
// Every rule below is a compile error with its message.


// A command: its name, help text, handler and arguments, in the order the handler takes them.
template<typename Handler, typename... Arguments>
struct CommandItem {
	std::string_view name;
	std::string_view help;
	Handler handler;
	CommandPack<Arguments...> arguments;
};


// A group: a command set declared in its own header, nested under a name. Set is the CommandSet type of the group.
template<typename Set>
struct GroupItem {
	std::string_view name;
	std::string_view help;
};


// The handler of everything without its own handler.
template<typename Handler>
struct DefaultItem {
	Handler handler;
};


// The handler of Ctrl+C, which runs at once.
template<typename Handler>
struct InterruptItem {
	Handler handler;
};


// The parts of a command handler's type: whether it has the right shape, its context type and its parameters after the call.
template<typename Handler>
struct CommandHandlerOf {
	static constexpr bool Valid = false;
	static constexpr bool Long = false;
};


template<typename Context, typename... Parameters>
struct CommandHandlerOf<ResultStatus (*)(CommandCall<Context>, Parameters...)> {
	static constexpr bool Valid = true;
	static constexpr bool Long = false;
	using Ctx = Context;
	using Values = std::tuple<std::remove_cvref_t<Parameters>...>;
};


// A long command's handler: it returns a CommandStep, and its first parameter carries the handler's state (LongCommands).
template<typename Context, typename Saved, typename... Parameters>
struct CommandHandlerOf<CommandStep (*)(CommandLongCall<Context, Saved>, Parameters...)> {
	static constexpr bool Valid = true;
	static constexpr bool Long = true;
	using Ctx = Context;
	using State = Saved;
	using Values = std::tuple<std::remove_cvref_t<Parameters>...>;
};


template<typename Handler>
struct CommandDefaultOf {
	static constexpr bool Valid = false;
};


template<typename Context>
struct CommandDefaultOf<void (*)(CommandCall<Context>, const CommandEvent&)> {
	static constexpr bool Valid = true;
	using Ctx = Context;
};


template<typename Handler>
struct CommandInterruptOf {
	static constexpr bool Valid = false;
};


template<typename Context>
struct CommandInterruptOf<void (*)(Context&)> {
	static constexpr bool Valid = true;
	using Ctx = Context;
};


template<typename Item>
inline constexpr bool CommandIsDefault = false;


template<typename Handler>
inline constexpr bool CommandIsDefault<DefaultItem<Handler>> = true;


template<typename Item>
inline constexpr bool CommandIsInterrupt = false;


template<typename Handler>
inline constexpr bool CommandIsInterrupt<InterruptItem<Handler>> = true;


// The checks on a command's arguments: the optional ones come last, the names are different, and each declaration is valid.
template<typename... Arguments>
constexpr void CommandCheckArguments(const Arguments&... arguments) {
	std::array<bool, sizeof...(Arguments)> optional{arguments.hasDefault...};
	std::array<std::string_view, sizeof...(Arguments)> names{arguments.name...};

	bool seenOptional = false;
	for (bool isOptional : optional) {
		if (isOptional) {
			seenOptional = true;
		} else {
			CommandRequire<CommandText{"a required argument comes after an optional one"}>(!seenOptional);
		}
	}

	for (std::size_t index = 0; index < names.size(); index++) {
		for (std::size_t other = 0; other < index; other++) {
			CommandRequire<CommandText{"two arguments of a command have the same name"}>(names[index] != names[other]);
		}
	}

	(CommandRequireName(arguments.name), ...);
	(arguments.Verify(), ...);
}


// The handler must take CommandCall<Ctx> first, and its other parameters must be the value types of the arguments, in order.
template<typename Handler, typename... Arguments>
constexpr void CommandCheckHandler() {
	CommandRequire<CommandText{"a command handler returns ResultStatus and takes CommandCall<Ctx> first"}>(CommandHandlerOf<Handler>::Valid);

	if constexpr (CommandHandlerOf<Handler>::Valid) {
		CommandRequire<CommandText{"the handler parameters do not match the arguments"}>((std::is_same_v<typename CommandHandlerOf<Handler>::Values, std::tuple<typename Arguments::Value...>>));
	}
}


template<typename... Items>
class CommandSetBuilder {
public:
	CommandPack<Items...> items{};


	constexpr CommandSetBuilder() = default;


	constexpr explicit CommandSetBuilder(CommandPack<Items...> itemList) : items(itemList) {
	}


	// A command with its help text, its handler and its arguments.
	template<typename Handler, typename... Arguments>
	constexpr CommandSetBuilder<Items..., CommandItem<Handler, Arguments...>> Command(
		std::string_view name,
		std::string_view help,
		Handler handler,
		Arguments... arguments) const
	{
		CommandRequireName(name);
		CommandRequire<CommandText{"a command has no help text"}>(!help.empty());
		CommandCheckHandler<Handler, Arguments...>();
		CommandCheckArguments(arguments...);

		CommandItem<Handler, Arguments...> item{name, help, handler, CommandPackOf(arguments...)};
		return CommandSetBuilder<Items..., CommandItem<Handler, Arguments...>>(CommandPackAppend(items, item));
	}


	// A group, a command set declared in another header. Written as Group<MotorCommands>("motor", "motor control"), since the set is
	// a type. The group must have at least one command or group.
	template<typename Set>
	constexpr CommandSetBuilder<Items..., GroupItem<Set>> Group(std::string_view name, std::string_view help) const {
		CommandRequireName(name);
		CommandRequire<CommandText{"a group has no help text"}>(!help.empty());
		CommandRequire<CommandText{"an empty group has no commands or groups"}>(Set::EntryCount > 0);
		CommandRequire<CommandText{"a group has no Default() or OnInterrupt(), the top set handles those"}>(!Set::HasDefault && !Set::HasInterrupt);

		GroupItem<Set> item{name, help};
		return CommandSetBuilder<Items..., GroupItem<Set>>(CommandPackAppend(items, item));
	}


	// The handler of Ctrl+C: void(Ctx&). It runs inside Feed(), so it must be short and safe to run in an interrupt.
	template<typename Handler>
	constexpr CommandSetBuilder<Items..., InterruptItem<Handler>> OnInterrupt(Handler handler) const {
		constexpr bool given = (CommandIsInterrupt<Items> || ...);
		CommandRequire<CommandText{"OnInterrupt() is given twice"}>(!given);
		CommandRequire<CommandText{"an interrupt handler is void(Ctx&)"}>(CommandInterruptOf<Handler>::Valid);

		InterruptItem<Handler> item{handler};
		return CommandSetBuilder<Items..., InterruptItem<Handler>>(CommandPackAppend(items, item));
	}


	// The handler of everything without its own handler: void(CommandCall<Ctx>, const CommandEvent&).
	template<typename Handler>
	constexpr CommandSetBuilder<Items..., DefaultItem<Handler>> Default(Handler handler) const {
		constexpr bool given = (CommandIsDefault<Items> || ...);
		CommandRequire<CommandText{"Default() is given twice"}>(!given);
		CommandRequire<CommandText{"a default handler is void(CommandCall<Ctx>, const CommandEvent&)"}>(CommandDefaultOf<Handler>::Valid);

		DefaultItem<Handler> item{handler};
		return CommandSetBuilder<Items..., DefaultItem<Handler>>(CommandPackAppend(items, item));
	}
};
