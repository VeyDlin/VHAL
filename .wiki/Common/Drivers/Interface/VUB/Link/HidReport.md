# VUB::HidReport

The report layout of a HID packet link, as two functions: `HidReportPack` writes a payload into a report, and `HidReportPayload` reads the
payload of a received report. Header: `Common/Drivers/Interface/VUB/Link/HidReport.h`. The same layout is on the device (the
[HidLink](HidLink) writes and reads it) and on the PC, where a PC API puts the report ID in front of the report (ID 0) outside this layout.

## The layout

| Byte | Content |
|---|---|
| 0 | the payload's length, n, from 1 to the report's size minus 1 |
| 1 to n | the payload |
| n + 1 to the report's size minus 1 | zero |

Byte 0 is the only check: a report whose length is 0, or larger than the bytes after it, is not a packet. The padding is not checked.

## HidReportPack

```cpp
inline Result<std::size_t> HidReportPack(std::span<const uint8> payload, std::span<uint8> report);
```

Writes the payload and the length, and zeroes the rest of the report. Returns the report's size. Returns `invalidParameter` for a report
under two bytes, an empty payload, a payload that does not fit after the length byte, or one longer than 255 bytes.

## HidReportPayload

```cpp
inline Result<std::span<const uint8>> HidReportPayload(std::span<const uint8> report);
```

Returns the n bytes after the length byte. Returns `invalidData` for an empty report, a length of 0, or a length larger than the bytes after
it.

## Example

A payload of `A1 B2 C3` in a 64-byte report:

```text
03 A1 B2 C3 00 00 ... 00     (64 bytes: the length, the payload, 60 zero bytes)
```

The payload of that report is `A1 B2 C3`, and the report of an empty payload is refused, not written.

## Tests

`TestVubHidReport.cpp` checks the bytes that `HidReportPack` writes for a short payload, the bounds of the payload, the payload that
`HidReportPayload` reads back (and the lengths it refuses), and a round trip of every payload length of a 64-byte report.
