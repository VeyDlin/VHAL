# CommandNameTree

`Common/Utilities/CommandLine/CommandNameTree.h`: the prefix tree of names. Each character moves one node index, and no text is kept.

---

## Nodes

```cpp
struct CommandTreeNode {
	uint16 firstChild;   // first child, CommandNoNode when none
	uint16 sibling;      // next child of the same parent
	uint16 payload;      // the entry this name leads to, or the enum value index; CommandNoNode for a prefix
	char character;
};
```

A node's children are a linked list through `sibling`, in declaration order. The builder adds a new child at the end of the list, after the siblings declared before it, so the first child is the first name declared.

---

## Names

- `CommandIsNameCharacter(c)`: `a-z`, `0-9`, `_` and `-`.
- `CommandRequireName(name)`: a compile-time check. The name must not be empty and must use name characters only.
- `CommandMaxNameLength`: 32. An enum value name may not be longer.

---

## Stepping

`CommandTreeStep(nodes, node, c)` returns the child of `node` that holds `c`, or `CommandNoNode`. `CommandSet::TreeStep` calls it on the set's own level.

---

## Building

`CommandTreeBuilder<Capacity>` builds a level's trees in an array of `Capacity` nodes. `AddRoot()` starts a tree, and `Insert(root, name, payload)` stores the payload at the end of a name. `Insert` returns `false` when the end already has a payload, which is a duplicate name. The level copies the used nodes to an array of exact size.
