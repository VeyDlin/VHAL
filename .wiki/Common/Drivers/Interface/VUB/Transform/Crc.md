# VUB::Crc

A CRC transform. On send it appends the CRC of the packet; on receive it checks the CRC and removes it. It works on the packet as the
transform receives it, so the headers the blocks above added are covered too. Header: `Common/Drivers/Interface/VUB/Transform/Crc.h`.

The CRC is a type of the shared CRC library, a `Checksum::Crc`. Its parameters, its engines and its presets are on the library's page,
[Checksum::Crc](../../../../Utilities/Checksum/CRC/Crc.md).

## The chain

`VUB::Crc<CrcType, order>` takes a `Checksum::Crc` type and a byte order. The order is `VUB::CrcOrder::lowByteFirst` (the default) or
`VUB::CrcOrder::highByteFirst`.

```cpp
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>

.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
.Transform<VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>>()
```

Any CRC of width 1 to 64 bits is a `Checksum::Crc`, with its parameters written as a `Checksum::CrcParameters()` chain. A type that is not a
`Checksum::Crc` is a compile error that names what to pass.

The engine is the one of the CRC type. A `Checksum::Crc` uses the bitwise engine unless it names another: `Checksum::Crc16Modbus::With<Checksum::CrcTable>`
is the table engine. A chain pays for a table only when it names one.

## Wire format

On send the packet comes first, unchanged. The CRC follows: `growth` bytes, the width rounded up to whole bytes. The CRC is in the low
bits of the value, and its bytes are in the order of the byte order.

- Width 8: one byte. Width 16: two bytes. Width 12: two bytes, the top four bits of the second byte are zero.
- Width 24: three bytes. Width 64: eight bytes.

Example: CRC-16/MODBUS of the nine bytes `31 32 33 34 35 36 37 38 39` ("123456789"). The check value of MODBUS is `4B37`, so the packet on
the wire ends with `37 4B` (low byte first, as Modbus RTU sends it), or with `4B 37` high byte first.

On receive, Decode checks the last `growth` bytes against the CRC of the bytes ahead of them, and returns the packet without them. A
mismatch is `crcError`; the stack drops the packet and counts it as `transformError`. A packet shorter than the CRC is `invalidData`.

## Byte order

The byte order is a type parameter, so the transform holds no data: `std::is_empty` is true for every CRC transform. The byte order is part of
the fingerprint, because it is part of the wire. The engine and the check value are not part of the fingerprint.

## Cost

The table gives the flash and RAM of a chain with COBS framing over a stream link, and the CRC-16/MODBUS transform added to it. The
costs are measured on Cortex-M4 and M0+ with [`size.sh`](../README#measured-configurations) (rows (b), (c) and (d)).

| Added to COBS over a stream link | Text, M4 | Text, M0+ | RAM, M4 and M0+ |
|---|---:|---:|---:|
| `VUB::Crc<Checksum::Crc16Modbus>` (the bitwise engine, the default) | 196 | 200 | 80 |
| `VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>>` (the table engine) | 700 | 704 | 80 |

The table engine costs 256 entries of the width in bytes in flash, 512 bytes for width 16, and the rest of its code. The bitwise engine has
no table. The RAM is the same for both: the transform holds no data, and the stack's buffers take the CRC's growth.

## Snippet

A packet link with two CRC chains: the default engine with high byte first, and the table engine. It is compiled and run by
`.tool/sim/TestVubCrcWiki.cpp`.

```cpp
#include <Drivers/Interface/VUB/LinkTraits.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <span>


// A packet link that sends whole packets: it takes each packet, or reports busy.
struct BoardLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

void OnPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}

// The packets of the board carry a CRC-16/MODBUS, high byte first. The default engine is the bitwise one.
using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc16Modbus, VUB::CrcOrder::highByteFirst>>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

// The same CRC with the table engine, for a product that has flash to spare for the table.
using Fast = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>, VUB::CrcOrder::highByteFirst>>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
```

The contract check of the [test kit](../TestKit/README) runs on CRC-16/MODBUS with each engine and with the high byte first, on
CRC-8/SMBUS, CRC-32/ISO-HDLC and CRC-16/RIELLO, and on CRC-32/JAMCRC through a chain. The widths of 5, 12, 24 and 64 bits are checked
against the catalogue's check values on the wire.
