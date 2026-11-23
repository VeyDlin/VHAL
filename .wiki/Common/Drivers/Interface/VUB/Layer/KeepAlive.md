# VUB::KeepAlive

The keep-alive of a link that cannot report its state, such as a UART or an RS-485 line. Such a link is always up as far as the stack
knows, so a dead peer goes unnoticed. The layer finds it: when nothing has been received from the peer for a quiet time it sends a ping,
and when nothing has been received for a down time it declares the peer down. The next packet received declares it up again.

Header: `Common/Drivers/Interface/VUB/Layer/KeepAlive.h`. It is a layer of one hop: it sits below the address and never forwards a
packet. It has a header of one byte.

## Packets

| Packet | Bytes | Meaning |
|---|---|---|
| data | `00` and the payload | an application packet; the layer adds the `00` |
| ping | `01` | asks the peer to answer |
| pong | `02` | answers a ping |

A packet whose first byte is none of these, or a ping or pong with more bytes, is refused and counted under `layerError`. The layer
counts it as life all the same (see below).

For example, the payload `31 32` goes on the wire as `00 31 32`. The ping is the single byte `01`, and its pong is the single byte `02`.

## Times

| Option | Meaning | Default |
|---|---|---|
| `QuietTime(us)` | the time with nothing received after which the layer pings | 1000000 (one second) |
| `DownTime(us)` | the time with nothing received after which the peer is down | 3000000 (three seconds) |

The options come after the layer in the chain, and are given in microseconds. The down time should be longer than the quiet time plus
the round trip of a ping, so that a quiet peer that is alive gets its pong before it is declared down. The layer does not check this.

The rules, from `Process`:

- A packet received since the last `Process` is life. It counts at the time of that `Process`, and any packet counts, including a
  packet of unknown kind.
- When nothing has been heard for the down time, and the peer is up, the layer declares the peer down. It stays down until a packet is
  received. That packet declares the peer up in the same `Process`.
- When nothing has been heard for the quiet time, and no ping was sent in the last quiet time, the layer sends a ping. This goes on while
  the peer is down, so a ping goes out at each quiet time. A ping counts as sent only when the outbox takes it; a ping that is refused is
  sent again on the next `Process`.
- A ping that is received is answered with a pong in the same `Process` call that takes it, when the outbox takes it; a pong that is refused
  is sent on the next `Process`. A pong is never answered.
- A busy peer, one that sends something inside every quiet time, is never pinged.
- A quiet peer that is alive answers each ping, and stays up.

Recovery after an outage is bounded by the quiet time: while the peer is down, each end pings at each quiet time, so the first ping
after the line returns goes at most one quiet time later, and the other end is up when that ping arrives.

## The physical link

On a link that reports its state, the layer asks the link at each `Process` through `outbox.LinkUp()`. The answer is the state the link
reports as of that `Process`. A link that does not report its state is always up, and the answer is true.

- While the link is down, the layer sends no ping and no pong, declares nothing, and drops what it heard and any pong that was due.
- The first `Process` that finds the link up starts both timers from its time. The first ping goes one quiet time later, unless
  something is heard before then, so a ping is never on the wire closer than a quiet time to the one before it.
- A link that falls and comes back between two `Process` calls is not seen. The timers run on as they do for any quiet link.

The layer declares `usesLinkState = true` to get the query. A chain whose layers do not declare it has the same size as before the query
was added (see Cost).

## Where it goes in the chain

The keep-alive is a layer above the link CRC and the framing, so a corrupted packet is dropped before the layer sees it, and does not
count as life. The chain from the top down is: the layers of the application, the keep-alive, the transforms, the framing, the link. A
transform below the keep-alive sees packets that the layer has not checked.

The keep-alive is the lowest layer of the chain. Its pings go through the layers below it, so no layer may be below it: a delivery layer
such as VUB::Reliable stops sending while the connection is down, and a ping that went through it would never leave, so after a lost first
session both ends wait for each other. Placed below the delivery layer, the pings go out, and the delivery layer sees only the
application's packets. The builder refuses a keep-alive with another layer below it, with the message "a layer that declares reportsPeer
must be the lowest layer of the chain". A chain holds one layer that reports its peer, so a second keep-alive is refused too (see
[Link state](../README#link-state)).

## Peer down and the queues

The layer's own packets go while only the peer is down (see [Link state](../README#link-state)), so the pings and pongs go while
the application's packets wait:

- `WhileDown(VUB::Down::Wait)` (the default): the application's queued packets wait while the peer is down and go in order after it is up.
  A `Send` queues until the queue is full, then returns `busy`.
- `WhileDown(VUB::Down::Fail)`: the queued packets are dropped and counted under `linkDown` when the peer goes down, and `Send` returns
  `noConnection` while it is down. A refused `Send` is not counted.

## Cost

Measured with one method, for a Cortex-M4:

- Compiler: `arm-none-eabi-g++` 15.2.1 with `-std=c++20 -Os -mcpu=cortex-m4 -mthumb -fno-exceptions -fno-rtti`.
- One translation unit that holds a global stack, and calls `Send`, `Process` and `FeedPacket` on it, so the code of the stack is in the
  object. Each chain is built alone.
- `sizeof` is the size of the global stack's symbol, from `arm-none-eabi-nm -S`. `text`, `data` and `bss` are from `arm-none-eabi-size`
  of the object file.
- The base chain is a packet link that does not report its state, `MaxPayload(40)`, `ReceiveQueue(4)`, the default `SendQueue`,
  `WhileDown(Wait)` and a clock. The layer chains are the same unless a row says otherwise.

Cost of the layer in the base chain:

| Chain | `sizeof` | `text` | `bss` |
|---|---|---|---|
| no layer | 648 | 624 | 660 |
| a layer that does nothing (no peer report, no link query) | 736 | 725 | 748 |
| `VUB::KeepAlive` (quiet 1000 us, down 3000 us) | 996 | 1313 | 1008 |
| cost of `VUB::KeepAlive` over no layer | +348 | +689 | +348 |
| `VUB::KeepAlive` (quiet 1000 us, down 3000 us) on a link that reports its state | 1000 | 1403 | 1009 |

The peer report alone, on the same layer type with `reportsPeer` cleared and set. This chain has a packet link, `MaxPayload(40)`, the
default queues and the default times:

| Chain | `sizeof` | `text` | `bss` |
|---|---|---|---|
| `VUB::KeepAlive` with `reportsPeer` cleared | 756 | 1087 | 768 |
| `VUB::KeepAlive` with `reportsPeer` set | 996 | 1317 | 1008 |
| the peer report | +240 | +230 | +240 |

The cost of a chain with the layer is all of what it adds to the chain without it. The first row of the cost table is the cost of a layer
that does nothing, and it is measured with the same queues. The table's keep-alive row includes the layer's own state and code, the
own-packet queue and link record of the peer report, and the layer's header byte in the queue slots. The shares of those parts of the
348 bytes were not measured. The peer report row uses the default times, 1 s and 3 s (1000000 us and 3000000 us), and the cost rows use
1000 us and 3000 us, so the rows are not added up.

A chain whose layers do not declare `usesLinkState` is unchanged by the query: with a packet link, with a link that reports its state, and
with a layer that declares `reportsPeer`, the `sizeof` and `text` are the same as before the query was added. Those chains were measured
with the same method, at the commit before the query and with it.

The cost is paid only by a chain that has the layer. A chain without it has no queue, no record and no step buffers from it.

## Example

The board's line cannot report its state. It is quiet for half a second and declared down after two seconds. The keep-alive is above the
link, and the application's packets wait while the peer is down.

```cpp
// A board on a packet link that cannot report its state. Quiet for half a second, declared down after two seconds.
struct BoardUart {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().Framed().MaxPacket(64).FullDuplex();


	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};


void OnBoardPacket(const VUB::Meta&, std::span<const uint8> payload) {
	(void)payload;
}


// The user's microsecond clock. A timer counter in a real device; a constant here.
uint32 BoardClock() {
	return 0;
}


using Board = VUB::Stack<+[] {
	return VUB::Builder()
		.Layer<VUB::KeepAlive>()
		.QuietTime(500000)
		.DownTime(2000000)
		.Link<BoardUart>()
		.MaxPayload(32)
		.OnReceive<&OnBoardPacket>()
		.Clock<&BoardClock>();
}>;


BoardUart boardUart;
Board board(boardUart);


void BoardLoop() {
	std::array<uint8, 2> hello = {0x31, 0x32};
	(void)board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
```

The tests are in `.tool/sim/TestVubKeepAlive.cpp`. They run the layer on the test kit's silent packet line, and on a line that reports its
state, with the line taken down and up. The three outages are: the line down from the start, the line down after the peer was declared
down, and the line down while the peer is up. Each checks that no two packets of the layer are closer than a quiet time on the wire, and
that the peers recover.
