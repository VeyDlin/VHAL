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

// Copy between arrays and structs
struct Packet { uint16 id; uint8 flags; };
uint8 raw[16];
Packet pkt;
ByteConverter::ToDataFromArrayCopy(pkt, raw);        // array → struct
ByteConverter::ToArrayFromDataCopy(raw, pkt);         // struct → array
Packet pkt2 = ByteConverter::FromArray<Packet>(raw);  // array → struct (returns copy)
```
