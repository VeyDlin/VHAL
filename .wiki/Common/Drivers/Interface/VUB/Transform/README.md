# Transforms

A transform changes a packet into another packet on send, and back on receive. It adds bytes (a CRC) or changes them (an encryption).

The transforms built in:

- [VUB::Crc](Crc): a CRC of width 8, 16 or 32 bits, appended on send and checked on receive. It has presets for the reveng CRC
  catalogue, and an engine for the computation ([engines](../Engine/README)).

A transform's contract is in [writing a block](../README#writing-a-block).
