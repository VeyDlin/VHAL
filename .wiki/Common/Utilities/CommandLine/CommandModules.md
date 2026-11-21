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
| `void OnPrompt(Console&)` | before the prompt is written | |

A `void OnLineEnd` is also accepted, and takes nothing.

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

Modules run in the order they are listed. List `Help` before any module that takes characters or events, because a character or event taken earlier never reaches `Help` (see CommandHelp.md).
