# CommandCheck

`Common/Utilities/CommandLine/CommandCheck.h`: the compile-time error mechanism of the command line. It is the same pattern as the FAT12 builder and the USB descriptor builders.

---

| Name | Meaning |
|---|---|
| `TextLiteral` | The message type, from `Utilities/CompileTime/TextLiteral.h`. A string literal is the template argument: `CommandRequire<"...">(condition)` |
| `CommandError<Message>()` | Declared, never defined. A call from a constant expression is an error, and the diagnostic prints the message |
| `CommandRequire<Message>(condition)` | Calls `CommandError<Message>()` when the condition is false |

A message must not contain an apostrophe. GCC escapes it inside the printed template argument, so the message no longer matches its text in the check script. The messages use "the context of a handler" and similar wording for this reason.

Every rule of the command line is raised through `CommandRequire`, so a rule can only fail in a constant expression, which is the chain evaluated by `CommandSet`.
