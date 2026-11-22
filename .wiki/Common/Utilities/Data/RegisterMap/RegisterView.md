# RegisterView

Header: `#include <Utilities/Data/RegisterMap/RegisterView.h>`

The entries a Modbus server serves: holding registers, input registers, coils and discrete inputs, each backed by user data.
The view owns no storage. Each entry points at a user variable with static storage, or at functions that read and write a
value.

```cpp
extern uint16 setpoint;
extern bool relayState;
bool CheckSetpoint(uint16 value);       // true when a write may store the value
uint16 ReadChipTemperature();

using Table = RegisterView<+[] {
	return RegisterViewBuilder()
		.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
		.InputRegisters(0x0010, Computed(&ReadChipTemperature))
		.Coils(0x0000, Variable(&relayState));
}>;

static_assert(Table::checked);
```

The argument of `RegisterView` is a captureless lambda that returns a `RegisterViewBuilder`. Each builder method returns a
new builder, and its type lists the entries, so the view knows every entry type at compile time.

## Entries

| Method | Table | Value types |
|---|---|---|
| `HoldingRegisters(address, source)` | holding, read and write | integers of 2, 4 or 8 bytes; arrays of 16-bit integers |
| `InputRegisters(address, source)` | input, read only | the same as holding |
| `Coils(address, source)` | coil, read and write | `bool`; `std::array<bool, N>` |
| `DiscreteInputs(address, source)` | discrete, read only | the same as coils |

A value of N words takes N registers, the most significant word first, each word big-endian. An array of 16-bit integers
takes one register per element. A `uint8` is not a register value: use a `uint16`.

## Sources

| Source | Read | Write |
|---|---|---|
| `Variable(&x)` | `x` | stores `x` |
| `Variable(&x, &check)` | `x` | stores `x` if `check(value)` is true |
| `Computed(&get)` | `get()` | none |
| `Computed(&get, &set)` | `get()` | `set(value)`, which returns a `ResultStatus` |
| `Computed(&get, &set, &check)` | `get()` | `set(value)` if `check(value)` is true |

`check` and `set` take the value's type. Only holding and coil entries are written; an input or discrete entry is read only,
and a `Computed` with a set function there is a compile error.

## Rules, checked at compile time

- Two entries of one table may not share an address: `two view entries overlap in the same table at one Modbus address`.
- A block may not run past Modbus address 0xFFFF: `a block runs past Modbus address 0xFFFF`.
- The value types above: `a register value is an integer of 2, 4 or 8 bytes, or an array of 16-bit integers`, and
  `a coil or discrete input is a bool or an array of bool`.

The checks run when the view is instantiated. `static_assert(Table::checked)` runs them, because an alias alone does not
instantiate the class.

## Lookup

The static functions give the server its lookup. They take the table and the address, so one view serves all four tables.

```cpp
std::array<uint16, 1> words{};
ResultStatus status = Table::ReadWords(RegisterTable::Holding, 0x0000, words);

std::array<uint16, 1> newValue{ 42 };
status = Table::WriteWords(RegisterTable::Holding, 0x0000, newValue);

std::array<bool, 1> coils{};
status = Table::ReadBits(RegisterTable::Coil, 0x0000, coils);
status = Table::WriteBits(RegisterTable::Coil, 0x0000, coils);
```

| Function | Use |
|---|---|
| `ReadWords(table, address, span<uint16>)` | registers of the holding or input table |
| `ReadBits(table, address, span<bool>)` | coils or discrete inputs |
| `ReadPackedBits(table, address, quantity, span<uint8>)` | coils or discrete inputs, packed least significant bit first into `(quantity + 7) / 8` bytes. Each entry is read once, so a computed value runs once per call. Used by the Modbus server |
| `WriteWords(table, address, span<const uint16>)` | writes holding registers |
| `WriteBits(table, address, span<const bool>)` | writes coils |

A read may cover part of an entry, and it returns `badAddress` when an address of the range has no entry. Nothing is read
into the output then, because the check runs before the copy.

A write is all or nothing, and the whole request is checked first. The request must cover each entry it touches completely.

| Status | Meaning, nothing written |
|---|---|
| `ok` | every entry was written |
| `badAddress` | an address has no entry, a range starts or ends inside an entry, or the range runs past 0xFFFF |
| `accessError` | the entry is read only |
| `invalidData` | an entry's check rejected a value |
| `error` | a set function failed during the write. The entries before it stay written |

## Notes

- Each entry reads its user data with a plain load. A variable an interrupt writes must be `volatile`, or its read function
  must take a critical section. The view adds none.
- A set function that fails in the middle of a batch leaves the earlier entries written. A device has no rollback.
