# CommandModules

`Common/Utilities/CommandLine/CommandModules.h`: the module hooks of a console and the feature checks that match a module's needs with the listed modules.

---

## Hooks

Each hook is optional, and takes the console first.

| Hook | Called | Return |
|---|---|---|
| `bool OnCharacter(Console&, char)` | before the parser, for each printable or control character | true consumes the character |
| `bool OnKey(Console&, CommandKey)` | for a key of an escape sequence | true consumes the key |
| `bool OnLineEnd(Console&)` | at each line end, after the echo of `\r\n` | true takes the line: no events are raised for it |
| `bool OnEvent(Console&, const CommandEvent&)` | before the set's default handler | true consumes the event |
| `bool OnPrompt(Console&)` | before the prompt is written; true means the module wrote the prompt, so the console writes no `> ` (a `void` one is also accepted, and takes nothing) | `Ansi` |

A `void OnLineEnd` is also accepted, and takes nothing.

Features, which a module provides without a hook, are `CommandLineBufferFeature` (LineEditing), `CommandLongFeature` (LongCommands), `CommandHiddenFeature` (HiddenInput) and `CommandFloatFeature` (FloatArguments). The console reads them with `CommandProvided`, and does the work the module names. A set that needs one of them, and a console that does not list it, is a compile error.

The dispatch functions are `CommandCallCharacter`, `CommandCallKey`, `CommandCallLineEnd`, `CommandCallEvent` and `CommandCallPrompt`. Each uses `if constexpr` with a `requires` check, so a hook a module does not have costs no code. The consumed hooks (character, key, event) stop at the first module that takes them. `CommandLine` loops over the modules with `CommandPackForEach`. `OnLineEnd` reaches every module, and the line is taken when any one of them takes it.

---

## Features

A module names what it provides as a type of its own, and what it needs in the same way:

```cpp
struct Echo {
	using Provides = EchoFeature;
	using Needs = EditFeature;
};
```

| Name | Meaning |
|---|---|
| `CommandModuleProvides<Feature, Module>()` | The module's `Provides` is `Feature` |
| `CommandProvided<Feature, Modules...>()` | A listed module provides `Feature` |
| `CommandModuleReady<Module, Modules...>()` | The module's `Needs` is provided by the listed modules. True when it has no `Needs` |

`CommandLine` has `static_assert((CommandModuleReady<Modules, Modules...>() && ...), "a module needs a feature that no listed module provides")`. The check is on the listed modules, in any order.

---

## Order

Modules run in the order they are listed. A module that takes a character, a key or an event (has `OnCharacter`, `OnKey` or `OnEvent`) must be listed after `Help`, unless it works on the line buffer (it provides `CommandLineBufferFeature`, or needs it). `CommandHelpFirst<Modules...>()` checks this, and `CommandLine` has the `static_assert` with `Help must be listed before a module that takes characters, keys or events`. The check uses the hooks' presence, not what they return, so it needs no console. See CommandHelp.md.

## Line buffer and Help

| Name | Use |
|---|---|
| `CommandLineBufferFeature` | The feature of a module that keeps the line in a buffer (`LineEditing`). `History` needs it |
| `CommandModuleOnBuffer<Module>()` | The module provides or needs the buffer |
| `CommandModuleKeepsCopy<Module>()` | The module is a line copy: it sets `KeepsLineCopy` (`Help` does) |
| `CommandModuleTakesInput<Module>()` | The module has `OnCharacter`, `OnKey` or `OnEvent`, checked with a `CommandHookProbe` console |
| `CommandHookProbe` | A console stand-in for the checks above |
