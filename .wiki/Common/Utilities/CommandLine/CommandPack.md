# CommandPack

`Common/Utilities/CommandLine/CommandPack.h`: a list of values of different types, as a recursive aggregate. The builder keeps its items in one, and each command keeps its arguments in one.

---

## Why not std::tuple

GCC 15 cannot evaluate a `std::tuple` value produced by a lambda that is called through a function pointer in a constant expression. The chain `CommandSet<+[] {...}>` is evaluated that way, and with a tuple the compiler reports `dereferencing a null pointer` with no message from the checks. A plain aggregate, `std::pair`, and `std::array` all evaluate correctly. So values use `CommandPack`, and `std::tuple` only appears as a type list (`CommandHandlerOf::Values`), where it is not evaluated.

---

## Operations

| Function | Gives |
|---|---|
| `CommandPackOf(values...)` | A pack of the values, in order |
| `CommandPackAppend(pack, item)` | A new pack with `item` at the end |
| `CommandPackForEach(pack, visitor)` | Calls `visitor(element)` for each element, in order. A mutable pack gives the visitor a mutable element, which the console's modules use |
| `CommandPackGet<I>(pack)` | Element `I` |
| `CommandPackElement<I, Ts...>::Type` | The type of element `I` |
