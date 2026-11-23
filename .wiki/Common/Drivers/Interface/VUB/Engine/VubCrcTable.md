# VubCrcTable

The table engine, and the default engine of the CRC transform. It computes the CRC one byte at a time, with one lookup in a
256-entry table.

## Flash

The table is built at compile time from the parameters, as `constexpr` data, so no code runs at start. It lives in flash:

| Width | Table | Bytes |
|---|---|---|
| 8 | 256 entries of 1 byte | 256 |
| 16 | 256 entries of 2 bytes | 512 |
| 32 | 256 entries of 4 bytes | 1024 |

Each algorithm used has its own table. Presets of one width share the loop code, but not the table.

## Loops

One loop per width, in plain functions that take the table pointer and the reflection flag. A reflected input takes the low byte of the
register as the index and shifts the register right; a normal input takes the top byte and shifts left.

## Use

`VubCrc<parameters>` uses this engine. It is stateless: `Supports` says yes for every width the transform takes, and `Run` computes the
whole CRC of the bytes it is given. The code size of this engine is in the size table of the [VUB page](../README#size).
