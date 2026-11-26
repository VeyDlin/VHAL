# VUB::BodyOf: bodies to bytes

A body is a struct that a request, a reply or an event carries. `VUB::BodyOf<T>` gives the wire layout of a body: its fields, in
order, each as little-endian bytes. The codec writes a body with `VUB::Encode` and reads one with `VUB::Decode`. A body is declared
once, next to its struct, and the command description takes the struct as a type (see [Commands](Commands)).

Header: `Common/Drivers/Interface/VUB/Requests/Body.h`.

## The layout

The layout is a specialization of `BodyOf`, with a `static consteval` function `Layout()`:

```cpp
// The request body of the motor start command: a speed, a direction, and a ramp that version 1 peers do not send.
namespace Motor {

struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
	uint16 rampMs = 300;
};

}


namespace VUB {

template<>
struct BodyOf<Motor::MotorStart> {
	static consteval auto Layout() {
		return Fields<Motor::MotorStart>()
			.Field<&Motor::MotorStart::speedRpm>()
			.Field<&Motor::MotorStart::direction>()
			.Optional<&Motor::MotorStart::rampMs>();
	}
};

}
```

`Fields<T>()` starts the layout of `T`. Each `.Field<&T::member>()` adds a required field, and `.Optional<&T::member>()` adds an
optional one. The fields go in the order of the wire. Every required field is ahead of every optional field: a `Field` after an
`Optional` is a compile error. A field that is not a member of `T` is a compile error too.

A body type that has no specialization is not a body, and a command that takes it is a compile error (see Errors). `VUB::NoBody` is
the body of a command with no body, and it has an empty layout.

## Field types

| Type | Wire | Notes |
|---|---|---|
| `bool` | 1 byte | 0 or 1; any other value is `invalidData` on decode |
| `int8`, `int16`, `int32`, `int64`, `uint8`, `uint16`, `uint32`, `uint64` | 1 to 8 bytes | two's complement, little-endian; the types of `Utilities/DataTypes.h` |
| `enum class` over one of those types, with an underlying type of up to 4 bytes | the size of the enum | the value is the underlying integer |
| `float` | 4 bytes | the IEEE 754 binary32 bit pattern (a `static_assert` checks `std::numeric_limits<float>::is_iec559`) |
| a nested struct with its own `BodyOf` | the nested layout, inline | every field of the nested body is required |
| `std::array<T, N>` of the types above | `N` times the element | `N` is a compile-time constant; the elements are in order |

A body names the fixed-width types only. Every other integer type is a compile error: `char`, `wchar_t`, `char8_t`, `char16_t` and
`char32_t` are refused, and so are `long`, `long long`, `size_t` and the rest, because their width differs between targets (a 32-bit
`long` on MinGW and ARM, a 64-bit `long` on LP64 Linux), so a device and a PC would disagree on the wire. The test is on the type, not on
its name: where `long` or `size_t` is the same type as one of the fixed-width types on a target, it is accepted on that target, and its
wire is the same there. A body should still name the fixed-width type. The message of the error names the fixed-width types.

A `double` field is a compile error: it is not in version 1, and a scaled integer is the replacement. Pointers, `std::string` and
variable-length data are not field types. A nested body with an `Optional` field is a compile error, because the bytes after it would
be read as its missing part.

## Sizes

Each layout has two sizes, in bytes, given at compile time by `VUB::BodyViewOf<T>`:

- `minSize`: the required fields only. A body of this size is the oldest form a peer can send.
- `maxSize`: every field. `Encode` writes a body of this size, and the layer sizes its largest packet from it.

For `MotorStart`, `minSize` is 3 and `maxSize` is 5. `VUB::NoBody` has both sizes at 0.

## Encode and Decode

```cpp
// Encodes into a span: returns the bytes written, or messageTooLong when the span is shorter than maxSize.
template<typename T>
Result<std::size_t> Encode(const T& value, std::span<uint8> bytes);

// Decodes a span into value: returns ok, or invalidData (see Errors).
template<typename T>
ResultStatus Decode(std::span<const uint8> bytes, T& value);
```

`Encode` writes every field, the optional ones included. `Decode` first sets `value` to `T{}`, so an optional field that the bytes do
not reach keeps the default of its struct. The sender of a body writes all of it, and the receiver reads the fields it knows.

```cpp
// Encodes MotorStart{1200, 1} and decodes the version one form, which has no ramp.
void EncodeAndDecode() {
	Motor::MotorStart start{};
	start.speedRpm = 1200;
	start.direction = 1;

	std::array<uint8, 16> bytes{};
	Result<std::size_t> written = VUB::Encode(start, std::span<uint8>(bytes));
	if (written.IsOk()) {
		// written.Value() is 5, and bytes holds B0 04 01 2C 01.
	}

	const std::array<uint8, 3> versionOne = {0xB0, 0x04, 0x01};
	Motor::MotorStart received{};
	if (VUB::Decode(std::span<const uint8>(versionOne), received) == ResultStatus::ok) {
		// received.rampMs is 300: the version one form has no ramp field.
	}
}
```

The examples below are the bytes of the requests design. `MotorStart{1200, 1}` with the default ramp encodes to `B0 04 01 2C 01`:
1200 is `B0 04` and 300 is `2C 01`, little-endian. The version one form `B0 04 01` decodes to the same speed and direction, and the ramp
keeps its default, 300.

| Value | Bytes |
|---|---|
| `MotorStart{1200, 1}`, ramp 300 | `B0 04 01 2C 01` |
| the version one form (no ramp) | `B0 04 01`, decodes with ramp 300 |
| `MotorReport{1200, 850}` | `B0 04 52 03` |
| `MotorHealth{60, 1, 2}` (`uint32`, `uint8`, `uint8`) | `3C 00 00 00 01 02` |
| `float` 1.0 in a body | `00 00 80 3F` |
| `bool` 1 in a body | `01` |

## Versioning

The rules for changing a body, so that two versions of a peer link:

1. A field added to a body is appended, and it is `Optional`. Its default is the default member initializer of the struct.
2. A peer reads the fields it knows, in order, while bytes remain. A body that ends with a required field missing is `invalidData`.
3. A longer body from a newer peer is accepted: the bytes after the last known field are ignored.
4. A required field is never removed or moved, and an existing field never changes its type or width.

## Errors

`Decode` returns `invalidData` when a required field is missing or cut in the middle, when a `bool` is not 0 or 1, and when a nested
field is cut. Trailing bytes are not an error. `Encode` returns `messageTooLong` when the span is shorter than `maxSize`, and writes
nothing.

Compile errors, each with its message:

| Error | Message (part) |
|---|---|
| a `Field` after an `Optional` | `a required field follows an Optional field` |
| a member of another class | `a field is not a member of the body type` |
| a `double`, also in an array | `a double field is not in version 1` |
| an enum wider than 4 bytes | `an enum field is at most 4 bytes` |
| a type that is not allowed | `a body field has a type that is not allowed` |
| a nested body with an `Optional` | `a nested body has an Optional field` |
| a float on a non-IEEE platform | `a float field needs IEEE 754 binary32 floats` |
| a type used as a body with no `BodyOf` | `a request body type has no BodyOf` (from the command) |

## Code and size

The codec is two functions, `DecodeBody` and `EncodeBody`, that work on a `BodyView` (the table of fields of a body). Every body has
its own table, and the functions are the same for all of them. A field's table entry is its width, kind, count and an accessor. Only the
accessor is per field.

Measured with `arm-none-eabi-g++ -std=c++23 -mcpu=cortex-m4 -mthumb -Os -fno-exceptions -fno-rtti` on a program that encodes and
decodes one body (`MotorStart`), and on the same program with a second body (`MotorReport`) added:

| Part | One body | Two bodies |
|---|---|---|
| `DecodeBody` (text, shared) | 260 bytes | 260 bytes |
| `EncodeBody` (text, shared) | 184 bytes | 184 bytes |
| the accessors of the bodies (text) | 10 bytes (3 fields) | 16 bytes (5 fields in all) |
| the layout tables of the bodies (rodata) | 84 bytes | 148 bytes |

The second body adds 6 bytes of text (its two accessors) and 64 bytes of tables, and no code of the codec is repeated. A body that is
not used adds nothing: its tables are not emitted.

## Not in version 1

- `double`, variable-length data, and pointers (see Field types).
- An optional field in a nested body.
- A fingerprint of the layout. The fingerprint of the protocol would count the required fields and the ids; an optional field would
  not change it.
