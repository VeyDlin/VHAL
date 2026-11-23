# VUB test kit

The test kit is for host tests. It is in `Common/Drivers/Interface/VUB/TestKit/` and is not part of a device build. A user who writes a
block uses it to check the block on a PC before it goes onto a board.

## Parts

| Header | What it gives |
|---|---|
| `VubRandom.h` | a deterministic generator (xorshift32): the same seed gives the same run |
| `VubWire.h` | `VubStreamWire` and `VubPacketWire`: one direction of a line that drops, corrupts, duplicates and delays items from a seed (`VubFaults`) |
| `VubLoopback.h` | link objects over the wires: `VubStreamEnd` (a byte stream, `Write` takes what the round budget allows) and `VubPacketEnd` (`Send` returns `busy` when the round budget is used, and `messageTooLong` for a packet longer than the line's `MaxPacket`); `VubStreamLine` and `VubPacketLine` join two ends |
| `VubContracts.h` | `VubCheckTransform` and `VubCheckFraming`, the contract checks |

## The loopback

A line has two directions. A stack on end `a` sends into one wire, and the stack on end `b` takes the wire's items when the test calls
`DeliverToB`:

```cpp
VubFaults faults;
faults.dropPercent = 5;
faults.corruptPercent = 5;
faults.duplicatePercent = 5;
faults.maxDelayUs = 50;
faults.seed = 7;

VubPacketLine<64, 512> line(faults);
// the stacks are built on line.a and line.b; each step sets the time and delivers
line.SetNow(now);
line.DeliverToB(receiverStack);
```

Each item is dropped, corrupted (one bit flipped), duplicated or delayed by a random amount up to `maxDelayUs`, so a later item can
arrive first. The same seed gives the same faults, and the same output on each run. The rates are the configured percentages over
many items (the tests check 10, 20 and 15 percent over 10000 and 20000 items within 1.5 points). A packet is never cut short: a packet
longer than the wire's packet size is not stored, and it is counted as overflowed. `Counts()` on a wire says how many items it dropped,
corrupted, duplicated and delivered, so a test can check that every packet is either handled or counted.

`StartRound(n)` on an end sets the room the link has until the next round: bytes for a byte-stream end, packets for a packet end. It
models a busy link: a byte-stream `Write` returns the count it took, and a packet `Send` returns `busy`.

## Contract checks

A user runs the checks on their own block. Each returns true when every round passes. The data come from a fixed seed, so a failure
repeats.

```cpp
XorTransform transform;
bool transformOk = VubCheckTransform(transform, 7, 500, 40);

FlagFraming framing;
bool framingOk = VubCheckFraming(framing, 3, 200, 32, 10);
```

`VubCheckTransform(transform, seed, rounds, maxPayload)` checks, on random packets of 0 to `maxPayload` bytes (at most 255):

- `Decode(Encode(x))` gives `x` back, and the encoded size is at most the length plus `growth`;
- a flipped bit in the encoded bytes makes `Decode` return an error;
- `Encode` and `Decode` write nothing past their spans: `Encode` gets a span of exactly the length plus `growth`, `Decode` a span of
  exactly the encoded length, and the guard bytes right after each span are checked after every call.

`VubCheckFraming(framing, seed, rounds, maxPayload, lossPercent)` frames random packets of 1 to `maxPayload` bytes (at most 32), pushes
the bytes one at a time, and loses `lossPercent` of them:

- every framed packet is at most its length plus `Growth(length)`, the size the stack sizes its framed buffer from;
- every packet that comes out is one of the packets sent, in the order sent, and whole;
- after the lossy part, three clean frames are sent, and the last two come out exactly.

A link is checked by running a stack over a line with faults: the packets that the handler gets, plus the drops that the stack counts,
must account for every item the wire delivered.

## What the checks do not do

The checks do not replace a test of the wire format. A block's bytes are written by hand in its tests, from the documented format, as the
VUB tests do (the wire bytes of a packet of the test chain are worked out in `TestVubStack.cpp`).

The checks use a fixed seed. A block that passes them for one seed is not proven; more rounds or another seed give more evidence.
