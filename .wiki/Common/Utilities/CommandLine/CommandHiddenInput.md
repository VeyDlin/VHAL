# CommandHiddenInput

`Common/Utilities/CommandLine/CommandHiddenInput.h`: the `HiddenInput` module and the `Secret<N>` argument. A secret is typed like a string, but each character is echoed as `*`, so a password does not show on the screen. Listed as `CommandLine<Set, App, Help, HiddenInput>`.

---

## Declaration and handler

```cpp
ResultStatus Login(CommandCall<AppContext> call, String<16> user, Secret<16> password) {
	call.app.Authenticate(user.View(), password.View());
	return ResultStatus::ok;
}

using AppCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("login", "log in", &Login, Argument<String<16>>("user"), Argument<Secret<16>>("password"));
}>;
```

- `Secret<N>` is a `String<N>`. The handler reads the characters with `View()`, and the value is the same as a `String<N>`'s.
- A secret has no `Default()`. A secret is required, like any argument without a default.
- A secret may be quoted, as a string is. A space inside the quotes is part of the secret, and it is hidden like the other characters.

---

## Rules

- Each character of a secret is echoed as `*`. Inside the quotes of a secret every character is echoed as `*`: the quotes, the spaces and the escapes too, so the length of a quoted secret, and where its spaces are, do not show. The space that ends an argument is echoed as a space, so the number of words stays readable. A space in the gap before an argument is echoed as a space too.
- Example: `login bob "a b"` shows `login bob *****`, five stars for the quotes, the two letters and the space.
- Backspace erases one `*`, as it erases one character.
- A secret past its capacity is still echoed as `*`. The error names the argument: `login: password must be at most 8 characters`.
- A line that holds a secret is not kept by `History`. Up does nothing when the line being typed holds a secret, because the draft cannot be kept.
- `LineEditing` draws the tail of a line that holds a secret as `*` for each character, so a cursor move that brings a secret back to the screen does not print it. The tail includes characters that are not secret when the line holds one. The rule applies to the whole tail, not only the secret.
- A secret that was typed and then erased still makes its line a secret line, so that line is not kept in history.
- After a name that matches no command, the rest of the line is hidden, spaces too, so a password typed after a misspelt name is not echoed. The line is then a secret line for History.
- Past the last argument of a command with a `Secret`, the text is hidden as `*`, spaces excepted, so `login bob pass word` shows `login bob **** ****` and the words of a password typed with a space in it do not show. The error is `login: too many arguments`. A command with no `Secret` shows its extra text as typed.
- A line that starts with `help` is not hidden when `Help` is listed, because its words are not secrets. After the word `help`, the arguments of the line are echoed as typed, so a secret typed after `help` is not hidden. Without `Help` listed, `help` is an unknown name, and the rest of the line is hidden.
- What still shows: the spaces that end an argument, so the number of words is visible; the length of a secret, one `*` per character; and, as above, a secret typed after `help`.
- A secret is never printed by the console. The error and help texts name the argument, not its value.

---

## Feature, not hook

The echo belongs to the console: it decides which character goes to a secret argument, and it has the line's state. So `HiddenInput` is a feature (`CommandHiddenFeature`) rather than a hook. A set with a `Secret` argument needs the module listed, and the header included, otherwise:

- without the header: `Argument<Secret<8>>` names an unknown type (the header declares it);
- with the header, but not listed: `a Secret argument needs the HiddenInput module, listed in the console`.

A set without a secret has no secret code, and `HiddenInput` costs one byte in the console's module pack.

---

## Structure

| Part | Use |
|---|---|
| `struct Secret<Capacity>` | The secret's value, a `String<Capacity>` |
| `Argument<Secret<Capacity>>` | The declaration. `Kind` is `Secret`. It has no default |
| `CommandSlotOf<Argument<Secret<C>>>` | The reader: `CommandTextSlot<C>`, the same reader as a string. The parser hides its characters from the argument's kind |
| `struct HiddenInput` | The module. `Provides` is `CommandHiddenFeature` |

The console echoes a secret character through `CommandParser::HidesNext`, which asks whether the character goes to a secret argument. `CommandParser::HoldsSecret` says whether a secret has been typed on the line. `CommandLine::Echo` writes the line's text for a module that redraws it, and masks it when the line holds a secret.

See `CommandLineEditing.md` and `CommandHistory.md` for the editing modules' side.
