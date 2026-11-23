# VubCrcBitwise

The bitwise engine. It computes the CRC one bit at a time, with no table, so an algorithm costs only the code of the loop, which
is shared by every preset.

It is slower than the table: about eight shift-and-XOR steps per byte. It is the choice for a part with no flash for the table.

It is stateless and computes every CRC of width 8, 16 or 32, so `Supports` says yes for all of them. Use it through the second
template argument of the transform: `VubCrc<VubCrc16Modbus::parameters, VubCrcBitwise>`. Both engines give the same CRC; the tests check
that they agree on random data.
