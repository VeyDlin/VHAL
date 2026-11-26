# CommandCheck

The compile-time checks of the command line are `Require` checks from `Utilities/CompileTime/Require.h`. Their messages start with `CommandLine: `.

---

| Name | Meaning |
|---|---|
| `Require<Message>(condition)` | A string literal is the template argument: `Require<"CommandLine: ...">(condition)`. The check calls `CompileTimeError<Message>()` when the condition is false, and a call from a constant expression is an error that prints the message |

A message must not contain an apostrophe. GCC escapes it inside the printed template argument, so the message no longer matches its text in the check script. The messages use "the context of a handler" and similar wording for this reason.

Every rule of the command line is raised through `Require`, so a rule can only fail in a constant expression, which is the chain evaluated by `CommandSet`.
