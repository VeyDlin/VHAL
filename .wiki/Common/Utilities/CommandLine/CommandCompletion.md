# CommandCompletion

`Common/Utilities/CommandLine/CommandCompletion.h`: the `Completion` module. Tab completes the token at the end of the line from the command set's name tree. Listed as `CommandLine<Set, App, Help, LineEditing<64>, Completion>`. It works with and without `LineEditing`.

---

## What Tab completes

The end of the line is one of:

- a command or group name at the current level: the start of the line, a group name followed by a space, or a name typed so far;
- an enum value, for the next argument of a command, when that argument is an enum: after the command's name and a space, or while the value is typed.

Other tokens (integers, bools, strings) complete nothing. A line whose last name matches no command also completes nothing. Word length does not limit it, since it walks the tree.

---

## Rules

- A unique continuation is typed in. `ke` and Tab gives `key`. Nothing is added after it, not even a space.
- Several continuations: the part they all share is typed in (possibly nothing), and the next Tab with no change to the line in between lists them. The list goes on a new line, two spaces apart, in declaration order, as the set declares them (the same order Help uses). Then the prompt and the line are written again.
- "No change to the line" means the console's change count is the same as after the Tab that typed the shared part. Any typed character, Backspace, or a line end starts again.
- Without `LineEditing`, the second Tab with no change to the line rings the bell (`0x07`) and lists nothing. Each further Tab with no change rings it again. A change to the line starts again, as above.
- With the cursor before the end, Tab first moves the cursor to the end (through `LineEditing::ToEnd`), then completes.
- A Tab where nothing can be completed does nothing. The Tab is taken: it is never typed as a space when `Completion` is listed.
- A set with a default handler only, and no commands or groups, takes no Tab: the Tab is typed as a space.

---

## Output

| Case | Output |
|---|---|
| Unique | The continuation, echoed |
| Shared part, first Tab | The shared part, echoed (nothing when empty) |
| Choices, second Tab, with `LineEditing` | `CRLF`, the choices separated by two spaces, `CRLF`, the prompt, the line |
| Choices, second Tab, without `LineEditing` | `BEL` (`0x07`). Nothing is listed, and the line is not written again |

For example, in a set that declares `move`, then `mode`, then `motor`, `m` and Tab types `o`, and a second Tab lists `move  mode  motor`. Without `LineEditing` the same keys give `mo` and then `BEL`.

Without `LineEditing` the console keeps no text of the line, so it cannot write the line again, and a listing would leave the screen out of step with the parser. The bell is the one answer that needs nothing but the terminal. The parser still holds the line, so the next keys continue it.

---

## Structure

| Part | Use |
|---|---|
| `struct Completion` | The module. `OnCharacter` takes Tab. It has no `Provides` or `Needs` |
| `armed`, `armedChanges` | Whether the last Tab typed a shared part, and the change count after that |
| `Complete(console)` | Finds the token and the step, then types, lists or rings the bell |
| `CompletionFind<L>(from, target, text, depth)` | The path of characters from a tree node to another, found by a walk from the root |
| `CompletionNodeOf<L>(node, payload)` | The node below `node` that holds a payload |
| `CompletionNextPayload<L>(node, floor)` | The smallest payload at or above `floor` below `node`. The listing takes them in order, so the choices come in declaration order |
| `ListChoices<L>(console, point)` | Writes the choices, then the prompt and the line |

An enum argument's typed text is read through `CommandSet::EnumNode`, which knows the reader's type from the declaration. The parser gives the reader in the edit point, so the slot base has no virtual member for it. The walk down a single-child chain gives the shared part without a search. A name ends at a node with a payload, and a unique name is one where that node has no children.

---

## Limits

- The candidate text is held in a stack buffer of the longest name, so a listing of a long name needs that many bytes of stack.
- A list is one line. A long list is not wrapped.
- Each choice is found by a walk of the subtree, so a listing costs time in the number of choices times the size of the subtree. A command set is small, so this is fine, and it runs only on a second Tab.
- A console without `Completion` links none of this code. The enum lookup, `CommandSet::EnumNode`, is referenced only from here.
- Host size: 8 bytes for the object. Not measured on the G431.
