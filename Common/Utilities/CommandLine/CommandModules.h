#pragma once
#include "CommandInput.h"
#include "CommandTypes.h"
#include <array>
#include <concepts>
#include <cstddef>
#include <type_traits>


// Modules of a console. A module is a type listed after the application type of CommandLine. It adds behaviour through the hooks
// below, which are optional members. Each hook takes the console first, so a module reaches the console's output and application
// through it. A module that is not listed costs nothing: no code of it is built into the console.
//
//   bool OnCharacter(Console&, char character)   before the parser; true consumes the character
//   bool OnKey(Console&, CommandKey key)         a key from an escape sequence; true consumes it
//   bool OnLineEnd(Console&)                     at each line end, before the line's events; true takes the line, so its events are not raised
//                                                (a void OnLineEnd is also accepted, and takes nothing)
//   bool OnEvent(Console&, const CommandEvent&)  before the default handler; true consumes the event
//   bool OnPrompt(Console&)                      before the prompt is written; true means the module wrote the prompt, so "> " is not written
//                                                (a void OnPrompt is also accepted, and takes nothing)
//
// A module names the feature it provides as Provides, a type of its own. A module that needs a feature names it as Needs. Needs is
// met when a listed module provides it, and a module whose Needs is not met is a compile error.
//
// The line buffer is a feature of its own (CommandLineBufferFeature), provided by LineEditing. Help reads the line from it when it is
// listed, and History edits the line through it. Help keeps a copy of its own (KeepsLineCopy) for a console without a buffer. Help must
// be listed before every module that takes a character, a key or an event, except the modules that work on the buffer. That is a compile
// error otherwise.


// Whether a listed module provides the feature. A module without Provides provides nothing.
template<typename Feature, typename Module>
constexpr bool CommandModuleProvides() {
	if constexpr (requires { typename Module::Provides; }) {
		return std::is_same_v<typename Module::Provides, Feature>;
	} else {
		return false;
	}
}


template<typename Feature, typename... Modules>
constexpr bool CommandProvided() {
	return (CommandModuleProvides<Feature, Modules>() || ...);
}


// Whether the needs of one module are met by the listed modules. A module with no Needs has none.
template<typename Module, typename... Modules>
constexpr bool CommandModuleReady() {
	if constexpr (requires { typename Module::Needs; }) {
		return CommandProvided<typename Module::Needs, Modules...>();
	} else {
		return true;
	}
}


template<typename Module, typename Console>
bool CommandCallCharacter(Module& module, Console& console, char character) {
	if constexpr (requires { { module.OnCharacter(console, character) } -> std::convertible_to<bool>; }) {
		return module.OnCharacter(console, character);
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
		static_cast<void>(character);
		return false;
	}
}


template<typename Module, typename Console>
bool CommandCallKey(Module& module, Console& console, CommandKey key) {
	if constexpr (requires { { module.OnKey(console, key) } -> std::convertible_to<bool>; }) {
		return module.OnKey(console, key);
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
		static_cast<void>(key);
		return false;
	}
}


template<typename Module, typename Console>
bool CommandCallLineEnd(Module& module, Console& console) {
	if constexpr (requires { { module.OnLineEnd(console) } -> std::convertible_to<bool>; }) {
		return module.OnLineEnd(console);
	} else if constexpr (requires { module.OnLineEnd(console); }) {
		module.OnLineEnd(console);
		return false;
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
		return false;
	}
}


template<typename Module, typename Console>
bool CommandCallEvent(Module& module, Console& console, const CommandEvent& event) {
	if constexpr (requires { { module.OnEvent(console, event) } -> std::convertible_to<bool>; }) {
		return module.OnEvent(console, event);
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
		static_cast<void>(event);
		return false;
	}
}


// Returns true when the module wrote the prompt itself. A module that returns true replaces the console's "> ".
template<typename Module, typename Console>
bool CommandCallPrompt(Module& module, Console& console) {
	if constexpr (requires { { module.OnPrompt(console) } -> std::convertible_to<bool>; }) {
		return module.OnPrompt(console);
	} else if constexpr (requires { module.OnPrompt(console); }) {
		module.OnPrompt(console);
		return false;
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
		return false;
	}
}


// The feature of a module that keeps the line text in a buffer of its own (LineEditing).
struct CommandLineBufferFeature {
};


// The feature of the module that runs long commands (LongCommands). The console runs them only when it is listed.
struct CommandLongFeature {
};


// The feature of the module that echoes Secret arguments as '*' (HiddenInput). The console echoes them only when it is listed.
struct CommandHiddenFeature {
};


// The feature of the module that reads float and double arguments (FloatArguments).
struct CommandFloatFeature {
};


// A console stand-in for the hook checks. The hooks take the console as a template parameter, so a probe shows which hooks a module has
// without a console of the application.
struct CommandHookProbe {
};


// Whether a module has a hook that takes input: a character, a key or an event.
template<typename Module>
constexpr bool CommandModuleTakesInput() {
	return requires(Module& module, CommandHookProbe& probe) { module.OnCharacter(probe, char{}); }
		|| requires(Module& module, CommandHookProbe& probe) { module.OnKey(probe, CommandKey::Other); }
		|| requires(Module& module, CommandHookProbe& probe, const CommandEvent& event) { module.OnEvent(probe, event); };
}


// Whether a module works on the line buffer: it provides the buffer, or needs it.
template<typename Module>
constexpr bool CommandModuleOnBuffer() {
	if constexpr (CommandModuleProvides<CommandLineBufferFeature, Module>()) {
		return true;
	} else if constexpr (requires { typename Module::Needs; }) {
		return std::is_same_v<typename Module::Needs, CommandLineBufferFeature>;
	} else {
		return false;
	}
}


// Whether a module is the line copy of Help. A module sets KeepsLineCopy to say so.
template<typename Module>
constexpr bool CommandModuleKeepsCopy() {
	if constexpr (requires { Module::KeepsLineCopy; }) {
		return Module::KeepsLineCopy;
	} else {
		return false;
	}
}


// Whether Help is listed before every module that takes input, except the modules on the line buffer. Each module that takes input
// counts, so the check does not need the console.
template<typename... Modules>
constexpr bool CommandHelpFirst() {
	std::array<bool, sizeof...(Modules) + 1> copies{CommandModuleKeepsCopy<Modules>()..., false};
	std::array<bool, sizeof...(Modules) + 1> takers{(CommandModuleTakesInput<Modules>() && !CommandModuleOnBuffer<Modules>() && !CommandModuleKeepsCopy<Modules>())..., false};

	bool copyAfter = false;

	for (std::size_t index = sizeof...(Modules); index > 0; index--) {
		std::size_t position = index - 1;

		if (takers[position] && copyAfter) {
			return false;
		}

		if (copies[position]) {
			copyAfter = true;
		}
	}

	return true;
}
