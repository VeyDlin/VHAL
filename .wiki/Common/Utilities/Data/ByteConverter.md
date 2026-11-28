# ByteConverter — Byte-Level Packing & Unpacking

Byte-level packing, unpacking, and memory operations.

```cpp
#include <Utilities/Data/ByteConverter.h>

// High/low byte extraction
uint8 hi = ByteConverter::GetHigh(uint16(0xABCD));  // 0xAB
uint8 lo = ByteConverter::GetLow(uint16(0xABCD));   // 0xCD

// Also works with uint32 → uint16
uint16 hi32 = ByteConverter::GetHigh(uint32(0x12345678));  // 0x1234
uint16 lo32 = ByteConverter::GetLow(uint32(0x12345678));   // 0x5678

// Create from high/low
uint16 val = ByteConverter::Create(uint8(0xAB), uint8(0xCD));     // 0xABCD
uint32 val32 = ByteConverter::Create(uint16(0x1234), uint16(0x5678));  // 0x12345678

// Set high/low in-place
ByteConverter::SetHigh(val, uint8(0xFF));  // 0xFFCD
ByteConverter::SetLow(val, uint8(0x00));   // 0xFF00

// Set the low or high 16 bits of a uint32 in place
uint32 wide = 0xAABBCCDD;
ByteConverter::SetLow(wide, uint16(0x1234));   // 0xAABB1234
ByteConverter::SetHigh(wide, uint16(0x5678));  // 0x56781234

// Extract typed value from byte array
uint8 buffer[] = {0x01, 0x02, 0x03, 0x04};
uint32 data = ByteConverter::GetType<uint32>(buffer);
uint32 dataRev = ByteConverter::GetType<uint32>(buffer, true);  // reversed byte order

// Get/set individual bytes
uint32 word = 0xAABBCCDD;
uint8 byte1 = ByteConverter::GetByte(word, 0);  // 0xDD (LSB)
uint8 byte3 = ByteConverter::GetByte(word, 3);  // 0xAA (MSB)

// Reverse byte order (endian swap)
ByteConverter::ReverseArray(word);  // 0xDDCCBBAA

// SetByte and GetByte use the same numbering on every host: position 0 is the least significant byte
uint32 target = 0xAABBCCDD;
ByteConverter::SetByte(target, uint8(0x11), 1);  // 0xAABB11DD

// Copy between arrays and structs
struct Packet { uint16 id; uint8 flags; };
uint8 raw[16];
Packet pkt;
ByteConverter::ToDataFromArrayCopy(pkt, raw);        // array → struct
ByteConverter::ToArrayFromDataCopy(raw, pkt);         // struct → array
Packet pkt2 = ByteConverter::FromArray<Packet>(raw);  // array → struct (returns copy)
```

The optional `startToCopyPosition` argument has different units. For `ToArrayFromDataCopy` it is a byte offset into the array. For `ToDataFromArrayCopy` and `FromArray` it is added to the source array pointer, so it counts elements of the source array (bytes for a `uint8` array).

## Byte order

`ByteOrder` is the one byte order of VHAL. It is declared at namespace scope:

```cpp
enum class ByteOrder : uint8 { little, big };
```

The forms below take the byte order as a template argument. They do not depend on the host's byte order: each byte is placed by
shifts. They are `constexpr` and header-only.

### Typed values

`Put<T, order>(bytes, offset, value)` writes `T` at `offset`, and `Get<T, order>(bytes, offset)` reads it. `T` is an integer
of 1 to 8 bytes (signed or unsigned) or an enum, which is written by its underlying type. `bool` and floating-point types are
a compile error. `bytes` is a `std::span<uint8>` (`std::span<const uint8>` for `Get`), or a `uint8*` (`const uint8*` for `Get`)
for code that already works on pointers.

```cpp
#include <Utilities/Data/ByteConverter.h>

uint8 frame[8] = {};
ByteConverter::Put<uint16, ByteOrder::little>(frame, 0, uint16(0x1234));   // 34 12
ByteConverter::Put<uint32, ByteOrder::big>(frame, 2, uint32(0xAABBCCDD));  // AA BB CC DD at 2..5
int16 signedWord = ByteConverter::Get<int16, ByteOrder::big>(frame, 2);    // 0xAABB, which is negative as int16

std::array<uint8, 4> packet{};
ByteConverter::Put<uint16, ByteOrder::big>(packet, 0, uint16(0x1234));    // span form of a std::array
uint16 back = ByteConverter::Get<uint16, ByteOrder::big>(packet, 0);      // 0x1234
```

The span forms have no run-time bounds check: the caller checks the range. An access past the end of a
`std::array` in a constant expression does not compile.

### Width forms

`Put<width, order>(bytes, offset, value)` and `Get<width, order>(bytes, offset)` take a width of 1 to 8 bytes as a template
argument. The value is a `uint64`; `Put` writes the low `width` bytes and `Get` returns them.

```cpp
ByteConverter::Put<3, ByteOrder::little>(frame, 0, uint64(0x123456));     // 56 34 12
uint64 field = ByteConverter::Get<3, ByteOrder::little>(frame, 0);        // 0x123456
```

A width given at run time uses the same bytes. It is `Put<order>(bytes, offset, value, width)` and
`Get<order>(bytes, offset, width)`, with a width of 1 to 8 that the caller keeps in range.

```cpp
ByteConverter::Put<ByteOrder::big>(frame, 0, uint64(0xABCD), 2);          // AB CD
uint64 read = ByteConverter::Get<ByteOrder::big>(frame, 0, 2);            // 0xABCD
```

### Arrays of bytes

`ToBytes<T, order>(value)` returns a `std::array<uint8, sizeof(T)>`. `FromBytes<T, order>(bytes)` reads the first `sizeof(T)`
bytes of an array or span. Both are `constexpr`, so a constant can be checked at compile time.

```cpp
std::array<uint8, 2> raw = ByteConverter::ToBytes<uint16, ByteOrder::big>(uint16(0x1234));   // 12 34
uint16 again = ByteConverter::FromBytes<uint16, ByteOrder::big>(raw);                        // 0x1234

constexpr std::array<uint8, 2> packed = ByteConverter::ToBytes<uint16, ByteOrder::little>(uint16(0x1234));   // 34 12
static_assert(packed[0] == 0x34);
```

`FromBytes` does not check the size of a span: the caller passes at least `sizeof(T)` bytes.
