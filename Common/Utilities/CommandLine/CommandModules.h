#pragma once
#include "CommandInput.h"
#include "CommandTypes.h"
#include <concepts>
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
//   void OnPrompt(Console&)                      before the prompt is written
//
// A module names the feature it provides as Provides, a type of its own. A module that needs a feature names it as Needs. Needs is
// met when a listed module provides it, and a module whose Needs is not met is a compile error.


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


template<typename Module, typename Console>
void CommandCallPrompt(Module& module, Console& console) {
	if constexpr (requires { module.OnPrompt(console); }) {
		module.OnPrompt(console);
	} else {
		static_cast<void>(module);
		static_cast<void>(console);
	}
}
