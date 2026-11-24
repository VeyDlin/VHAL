# VUB::Crc

A CRC transform. On send it appends the CRC of the packet; on receive it checks the CRC and removes it. It works on the packet as
the transform receives it, so the headers the blocks above added are covered too.

## Wire format

On send the packet comes first, unchanged. The CRC follows: `width / 8` bytes, the least significant byte first.

- Width 8: one byte.
- Width 16: low byte, then high byte.
- Width 32: bytes 0 to 3 of the CRC, least significant first.

In the high byte first form (see [Byte order](#byte-order)) the CRC bytes are in the reverse order.

Example: CRC-16/MODBUS of the nine bytes `31 32 33 34 35 36 37 38 39` ("123456789"). The check value of MODBUS is `4B37`, so the packet
on the wire ends with `37 4B` (the default, as Modbus RTU), or with `4B 37` in the high byte first form.

On receive, Decode checks the last `width / 8` bytes against the CRC of the bytes before them, and returns the packet without them.
A mismatch is `crcError`; the stack drops the packet and counts it as `transformError`. A packet shorter than the CRC is
`invalidData`.

## Byte order

The byte order is a type parameter: `VUB::Crc<parameters, engine, order>`, where `order` is `VUB::CrcOrder::lowByteFirst` (the default)
or `VUB::CrcOrder::highByteFirst`. `VUB::CrcHighByteFirst<parameters, engine>` is the high byte first form, and
`VUB::Crc16ModbusHighByteFirst` is CRC-16/MODBUS in that form.

The transform holds no data, so it takes no RAM: `std::is_empty` is true for every CRC transform. The byte order is part of the
fingerprint, because it is part of the wire. The engine is not.

## Presets

Each preset is a transform type named as the reveng CRC catalogue names it. The check value is the CRC of `"123456789"` (the ASCII
bytes `31` to `39`). Every preset is checked at compile time: a preset whose parameters do not give its check value does not compile. Each preset is written as
a `VUB::CrcParameters()` chain with its check value, the same chain as [Other parameters](#other-parameters).

| Preset | Width | Polynomial | Initial | Reflect in | Reflect out | Final XOR | Check |
|---|---|---|---|---|---|---|---|
| `VUB::Crc8Smbus` | 8 | 07 | 00 | no | no | 00 | F4 |
| `VUB::Crc8Ebu` | 8 | 1D | FF | yes | yes | 00 | 97 |
| `VUB::Crc8Maxim` | 8 | 31 | 00 | yes | yes | 00 | A1 |
| `VUB::Crc8Wcdma` | 8 | 9B | 00 | yes | yes | 00 | 25 |
| `VUB::Crc16Arc` | 16 | 8005 | 0000 | yes | yes | 0000 | BB3D |
| `VUB::Crc16Buypass` | 16 | 8005 | 0000 | no | no | 0000 | FEE8 |
| `VUB::Crc16CcittFalse` | 16 | 1021 | FFFF | no | no | 0000 | 29B1 |
| `VUB::Crc16Cdma2000` | 16 | C867 | FFFF | no | no | 0000 | 4C06 |
| `VUB::Crc16Cms` | 16 | 8005 | FFFF | no | no | 0000 | AEE7 |
| `VUB::Crc16DectR` | 16 | 0589 | 0000 | no | no | 0001 | 007E |
| `VUB::Crc16DectX` | 16 | 0589 | 0000 | no | no | 0000 | 007F |
| `VUB::Crc16Dnp` | 16 | 3D65 | 0000 | yes | yes | FFFF | EA82 |
| `VUB::Crc16Genibus` | 16 | 1021 | FFFF | no | no | FFFF | D64E |
| `VUB::Crc16Kermit` | 16 | 1021 | 0000 | yes | yes | 0000 | 2189 |
| `VUB::Crc16Maxim` | 16 | 8005 | 0000 | yes | yes | FFFF | 44C2 |
| `VUB::Crc16Modbus` | 16 | 8005 | FFFF | yes | yes | 0000 | 4B37 |
| `VUB::Crc16Riello` | 16 | 1021 | B2AA | yes | yes | 0000 | 63D0 |
| `VUB::Crc16T10Dif` | 16 | 8BB7 | 0000 | no | no | 0000 | D0DB |
| `VUB::Crc16Tms37157` | 16 | 1021 | 89EC | yes | yes | 0000 | 26B1 |
| `VUB::Crc16Usb` | 16 | 8005 | FFFF | yes | yes | FFFF | B4C8 |
| `VUB::Crc16X25` | 16 | 1021 | FFFF | yes | yes | FFFF | 906E |
| `VUB::Crc16Xmodem` | 16 | 1021 | 0000 | no | no | 0000 | 31C3 |
| `VUB::Crc32IsoHdlc` | 32 | 04C11DB7 | FFFFFFFF | yes | yes | FFFFFFFF | CBF43926 |
| `VUB::Crc32Bzip2` | 32 | 04C11DB7 | FFFFFFFF | no | no | FFFFFFFF | FC891918 |
| `VUB::Crc32C` | 32 | 1EDC6F41 | FFFFFFFF | yes | yes | FFFFFFFF | E3069283 |
| `VUB::Crc32Mpeg2` | 32 | 04C11DB7 | FFFFFFFF | no | no | 00000000 | 0376E6E7 |
| `VUB::Crc32Posix` | 32 | 04C11DB7 | 00000000 | no | no | FFFFFFFF | 765E7680 |
| `VUB::Crc32Q` | 32 | 814141AB | 00000000 | no | no | 00000000 | 3010BF7F |

The presets are those of `Crc.h` (the shared CRC library, `Utilities/Checksum/CRC/Crc.h`), plus CRC-16/RIELLO and CRC-16/TMS37157,
which that library does not have. Any other CRC of the catalogue whose width is 8, 16 or 32 (CRC-16/AUG-CCITT and CRC-16/MCRF4XX,
for example) is a custom `VUB::Crc` of a `VUB::CrcParameters()` chain with its check value, as in [Other parameters](#other-parameters).

The initial value is the value of the unreflected register, as the catalogue gives it, also when the input is reflected. CRC-16/TMS37157
and CRC-16/RIELLO have an initial value that is not symmetric, so their check values show that convention: a reflected CRC that
started from the initial value as it is would give another value. Both presets are checked with both engines.

A preset with an initial value of 0 and no final XOR (`VUB::Crc16Arc`, for one) has a zero residue: the CRC of a packet with its own
check bytes at the end is 0. So a packet made of two valid packets joined as one passes its check when the second packet's check is
the joined CRC. A byte stream that loses a COBS frame end joins two frames this way. `VUB::Crc16Modbus`, with an initial value of
0xFFFF, rejects the same join. On a byte stream, choose a preset whose initial value is not 0, as the link tests do. `TestVubLinkLoss` checks
this join with `VUB::Crc16Arc` and `VUB::Crc16Modbus`.

## Other parameters

A CRC that is not a preset is a chain of `VUB::CrcParameters()` methods, given as the template argument of `VUB::Crc`:

```cpp
// CRC-16/AUG-CCITT: polynomial 1021, initial 1D0F, not reflected, final XOR 0000, check E5CC.
inline constexpr VUB::CrcParameters augCcitt = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.Check(0xE5CC);

using AugCcitt = VUB::Crc<augCcitt>;
```

- `Width` and `Polynomial` are required. A chain without one is a compile error that names the method: "the CRC parameters have no
  Width" or "the CRC parameters have no Polynomial".
- `Width` takes 8, 16 or 32. Any other width is a compile error ("a CRC is 8, 16 or 32 bits wide").
- `Initial` and `FinalXor` default to 0. `ReflectIn` and `ReflectOut` default to false; called with no argument they set true, and
  `ReflectIn(false)` sets false.
- `Check` is optional. Without it no check runs. With it, the value must be the CRC of `"123456789"` with those parameters, in the
  reveng model. A wrong value is a compile error ("the check value of the CRC parameters does not match the CRC of 123456789"), and the
  diagnostic also lists the value the parameters give as hexadecimal digits, as chars: for E5CC it shows `'E', '5', 'C', 'C'`.
- The check is not part of the wire: the fingerprint is the same with or without `Check`.

## Engines

The second template argument is the engine:

- `VUB::CrcTable` (the default): one lookup per byte in a 256-entry table. The table is built at compile time and lives in flash:
  256 times the width in bytes, so 256 bytes for width 8, 512 for width 16 and 1024 for width 32.
- `VUB::CrcBitwise`: one bit at a time, no table. Its flash cost is only the code.

`VUB::Crc<VUB::Crc16Modbus::parameters, VUB::CrcBitwise>` is CRC-16/MODBUS with the bitwise engine. Both engines give the same CRC.

An engine says what it computes, through `Supports<Algorithm>()`. An engine that cannot compute the CRC is a compile error ("the engine
cannot compute this CRC"). Both built-in engines compute every CRC of width 8, 16 and 32.

The table engine's flash cost is its table, given above: 256 times the width in bytes. The bitwise engine has no table. The code size
of each engine is in the size table of the [VUB page](../README#size).

## Snippet

A packet link with two CRC chains: the default preset with high byte first, and the bitwise engine.

```cpp
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

// The packets of the board carry a CRC-16/MODBUS, high byte first. The table engine is the default.
using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc16ModbusHighByteFirst>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnPacket>();
}>;

// The same CRC with the bitwise engine, for a product with no flash to spare for the table.
using Small = VUB::Stack<+[] {
	return VUB::Builder()
		.Transform<VUB::Crc<VUB::Crc16Modbus::parameters, VUB::CrcBitwise>>()
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

Every preset's check value is checked at compile time. The tests also run the contract check of the [test kit](../TestKit/README)
on seven of the transforms: CRC-16/MODBUS with each engine and with the high byte first, CRC-8/SMBUS, CRC-32/ISO-HDLC, CRC-16/RIELLO
and CRC-32/JAMCRC through the chain (unchecked, bitwise engine).
