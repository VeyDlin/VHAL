# VUB test kit

The test kit is for host tests. It is in `Common/Drivers/Interface/VUB/TestKit/` and is not part of a device build. A user who writes a
block uses it to check the block on a PC before it goes onto a board.

## Parts

| Header | What it gives |
|---|---|
| `Random.h` | a deterministic generator (xorshift32): the same seed gives the same run |
| `Wire.h` | `VUB::StreamWire` and `VUB::PacketWire`: one direction of a line that drops, corrupts, duplicates and delays items from a seed (`VUB::Faults`) |
| `Loopback.h` | link objects over the wires: `VUB::StreamEnd` (a byte stream, `Write` takes what the round budget allows) and `VUB::PacketEnd` (`Send` returns `busy` when the round budget is used, and `messageTooLong` for a packet longer than the line's `MaxPacket`); `VUB::StreamLine` and `VUB::PacketLine` join two ends; a third parameter `true` makes the ends report the line's state, and `SetLinkUp` takes the line down and up (see [outages and reboots](#outages-and-reboots)) |
| `Contracts.h` | `VUB::CheckTransform` and `VUB::CheckFraming`, the contract checks |

## The loopback

A line has two directions. A stack on end `a` sends into one wire, and the stack on end `b` takes the wire's items when the test calls
`DeliverToB`:

```cpp
VUB::Faults faults;
faults.dropPercent = 5;
faults.corruptPercent = 5;
faults.duplicatePercent = 5;
faults.maxDelayUs = 50;
faults.seed = 7;

VUB::PacketLine<64, 512> line(faults);
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
bool transformOk = VUB::CheckTransform(transform, 7, 500, 40);

FlagFraming framing;
bool framingOk = VUB::CheckFraming(framing, 3, 200, 32, 10);
```

`VUB::CheckTransform(transform, seed, rounds, maxPayload)` checks, on random packets of 0 to `maxPayload` bytes (at most 255):

- `Decode(Encode(x))` gives `x` back, and the encoded size is at most the length plus `growth`;
- a flipped bit in the encoded bytes makes `Decode` return an error;
- `Encode` and `Decode` write nothing past their spans: `Encode` gets a span of exactly the length plus `growth`, `Decode` a span of
  exactly the encoded length, and the guard bytes right after each span are checked after every call.

`VUB::CheckFraming(framing, seed, rounds, maxPayload, lossPercent)` frames random packets of 1 to `maxPayload` bytes (at most 32), pushes
the bytes one at a time, and loses `lossPercent` of them:

- every framed packet is at most its length plus `Growth(length)`, the size the stack sizes its framed buffer from;
- every packet that comes out is one of the packets sent, in the order sent, and whole;
- after the lossy part, three clean frames are sent, and the last two come out exactly.

A framing with no check of its own (COBS, the length prefix) cannot promise whole packets with loss: a damaged frame can come out as
another packet. `VUB::CheckResync(framing, seed, rounds, maxPayload, lossPercent)` checks what such a framing can promise. It checks
the length of every packet against `maxPayload`, and after the lossy part it checks that the clean frames come out exactly: the first
one may be merged with the lossy part, and is not checked. Without loss, use `VUB::CheckFraming`.

A framing that uses time (the gap framing) is checked by `VUB::CheckTimedFraming(framing, seed, rounds, maxPayload, lossPercent, gapUs)`.
Each frame comes after a silence of three gaps, its bytes are spaced by less than a gap, and the check polls the framing at random
between them. Without loss every packet comes out whole and in order; with loss it is checked as `VUB::CheckResync` checks it.

The checks of the blocks, `VUB::Require` in the library, are declared and not defined. The test kit defines `VUB::CheckFailed` as a no-op, so a
block's `Growth` can be called at run time by the checks; a failed check stays a compile error in a constant expression.

A link is checked by running a stack over a line with faults: the packets that the handler gets, plus the drops that the stack counts,
must account for every item the wire delivered.

## Outages and reboots

`SetLinkUp(false)` takes a line down and `SetLinkUp(true)` brings it up. While it is down, both wires lose what is sent, and `Counts()`
counts it as `outaged`. A line with the state trait (the `true` parameter, as in `VUB::PacketLine<64, 512, true>`) tells both ends, so
their stacks learn of the outage at their next `Process`. A line without it is silent, as a UART is: its stacks learn nothing, and what
is sent meanwhile is lost.

A schedule of outages comes from the seed. `faults.outageForUs` is how long the line is down, and `faults.outageEveryUs` bounds the time
it is up before the next outage (the up time is drawn from the seed). The schedule runs in `SetNow`:

```cpp
// A line that reports its state, and a schedule of outages from the seed.
VUB::Faults faults;
faults.seed = 7;
faults.outageEveryUs = 500;
faults.outageForUs = 100;
VUB::PacketLine<64, 512, true> scheduled(faults);
scheduled.SetNow(0);
```

The line is up only when it is up by hand and up by its schedule. The two states are kept apart:

- A line taken down by hand (`SetLinkUp(false)`) stays down through every `SetNow`, with or without a schedule, until `SetLinkUp(true)`.
- A `SetNow` changes only the schedule's own state. A schedule that has the line up does not bring up a line that was taken down by hand.
- A line taken up by hand is still down while its schedule has it down, until the schedule's next up time.

A peer that reboots is a stack that is destroyed and built again on the same end, in the middle of a run. A reporting end gives its sink
back when its stack is destroyed, so an outage that comes while the peer is gone reports to no one. The pattern keeps the far stack in a
`std::optional`:

```cpp
// A peer that reboots: the far stack is destroyed and built again, and the near stack keeps working.
using Near = VUB::Stack<+[] {
	return VUB::Builder()
		.Link<VUB::PacketEnd<64, 512, true>>()
		.MaxPayload(32)
		.WhileDown(VUB::Down::Fail)
		.OnReceive<&OnPacket>()
		.Clock<&Now>();
}>;

VUB::PacketLine<64, 512, true> line(VUB::Faults{});
Near near(line.a);
std::optional<Near> far;
far.emplace(line.b);
far.reset();                 // the far stack is destroyed; its end gives the line back its sink
line.SetLinkUp(false);       // reported to no one
near.Process();              // near.LinkUp() is false
line.SetLinkUp(true);
far.emplace(line.b);         // the rebooted far stack
near.Process();              // near.LinkUp() is true again
far->Process();
```

With `WhileDown(VUB::Down::Fail)` the packets the near stack has queued while the line is down are dropped and counted as `linkDown`. The
[link state](../README#link-state) section has the rules.

## What the checks do not do

The checks do not replace a test of the wire format. A block's bytes are written by hand in its tests, from the documented format, as the
VUB tests do (the wire bytes of a packet of the test chain are worked out in `TestVubStack.cpp`).

The checks use a fixed seed. A block that passes them for one seed is not proven; more rounds or another seed give more evidence.
