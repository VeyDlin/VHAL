# CommandInput

`Common/Utilities/CommandLine/CommandInput.h`: turns the bytes of the input into what the console acts on. Each byte gives one `CommandInputStep`: nothing, a character, a line end, or a key of an escape sequence.

---

## Types

| Type | Meaning |
|---|---|
| `CommandInterruptByte` | `0x03`, Ctrl+C |
| `CommandKey` | `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `Delete`, `Other` |
| `CommandInputKind` | `Ignore`, `Character`, `LineEnd`, `Key` |
| `CommandInputStep` | The kind, the `byte` of a character and the `key` of a key |

---

## Step

`CommandInput::Step(byte)` returns the step for one byte:

- Inside an escape sequence, a byte of `0x20` or more is read by the sequence. A control byte ends the sequence and is then read as input.
- `0x1B` (ESC) starts an escape sequence, and gives `Ignore`.
- CR or LF is a line end. The byte that follows it, when it is the other one of the pair (CR LF, LF CR), is part of the same line end and gives `Ignore`. A repeated byte (two CRs, two LFs) is a new line end.
- Anything else is a `Character`.

### Sequences

Two forms are read: `ESC [` (CSI), with a decimal number and a final byte, and `ESC O` (SS3). Other second bytes end the sequence with no key.

- A CSI final `~` names a key by its number: `1` or `7` is `Home`, `3` is `Delete`, `4` or `8` is `End`. Other numbers give `Other`.
- Final bytes `A`, `B`, `C`, `D` are `Up`, `Down`, `Right`, `Left`, and `H` and `F` are `Home` and `End`, in both forms.
- Any other final byte gives `Other`. An `Other` key goes to the modules' `OnKey` hooks like the others, and the console does nothing with it if no module takes it.

`Reset()` forgets an open sequence and the last line end. `CommandLine` calls it on a terminal change.

---

## Limits

- A sequence's number is kept to four digits, so a long one cannot overflow.
- The keys are the ones a common terminal sends. Others are read as `Other`.
