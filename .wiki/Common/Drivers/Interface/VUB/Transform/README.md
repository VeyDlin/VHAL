# Transforms

A transform changes a packet into another packet on send, and back on receive. It adds bytes (a CRC) or changes them (an encryption).

The transforms built in:

- [VUB::Crc](Crc): a CRC of width 1 to 64 bits, appended on send and checked on receive. The CRC is a type of the shared CRC library,
  with its presets for the reveng CRC catalogue and its engines.

A transform's contract is in [writing a block](../README#writing-a-block).
