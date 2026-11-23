# VubReliable

Delivery layer. Each packet carries a sequence number, the session tag of its sender and the sender's window base. The receiver acknowledges each packet, and the sender resends a packet that is not acknowledged in time, up to a set number of times. With the in-order option a held packet is acknowledged when it goes up, and a copy of a held packet is not acknowledged (see In order). A session starts with a sync packet, which the receiver acknowledges before the sender sends any data. The sender needs a session value, given with `SetSession` (see Application), before its first send: until then `Send` returns `noInit`. The application learns the outcome of every packet it sent with delivery, once.

The layer is added with `.Layer<VubReliable>()` and its options follow it: `.Layer<VubReliable>().Retries(3).TimeoutMs(200).Window(4)`.

`VubReliable` is `VubReliableOf<32, 4>`: payloads up to 32 bytes, up to 4 packets unsettled. `VubReliableOf<PayloadCapacity, WindowCapacity, InOrder>` sets the storage. The options choose within it. The third argument turns on in-order delivery (see In order); it is `false` by default. The layer needs a clock: add `Clock<&Fn>()` to the chain.

A chain with the layer, the packet link of a board, and a handler for each outcome:

```cpp
// The packet link of the board: it sends a whole packet, or reports busy when its transmitter has no room.
struct BoardLink {
	static constexpr VubLinkTraits traits = VubLinkTraits().Framed().MaxPacket(64).FullDuplex();

	ResultStatus Send(std::span<const uint8> packet) {
		(void)packet;
		return ResultStatus::ok;
	}
};

// The clock of the board, in microseconds.
uint32 BoardClockUs() {
	return 0;
}

// Called once for each packet sent with delivery. The payload is valid only during the call.
void OnBoardOutcome(const VubOutcome& outcome) {
	(void)outcome;
}

void OnBoardRestart() {
}

void OnBoardPacket(const VubMeta&, std::span<const uint8> payload) {
	(void)payload;
}

using Board = Vub<+[] {
	return VubBuilder()
		.Layer<VubReliable>().Retries(3).TimeoutMs(200).Window(4).PayloadBytes(32)
		.OnOutcome<&OnBoardOutcome>()
		.OnRestart<&OnBoardRestart>()
		.Link<BoardLink>()
		.MaxPayload(32)
		.OnReceive<&OnBoardPacket>()
		.Clock<&BoardClockUs>();
}>;

BoardLink boardLink;
Board board(boardLink);

void BoardSend() {
	std::array<uint8, 3> hello = {1, 2, 3};
	board.Send(std::span<const uint8>(hello.data(), hello.size()));
	board.Process();
}
```

## Path contract

The layer is correct when the path between two peers keeps the order in which packets were sent. The path may lose some packets and duplicate some. Every VUB link (UART, USB, SPI, BLE, TCP) and a chain of forwarding nodes keeps the order.

A path that reorders packets (several routes, a retry queue below the layer) is safe only while all of these hold:

- no packet is held longer than the packet lifetime that the application states for that path;
- the sequence numbers do not wrap within that lifetime;
- after a reboot, a node waits one packet lifetime before its first send, so every packet of its earlier boot has left the
  path (as TCP's quiet time). Without the wait, a delayed acknowledgement of the earlier boot whose one-byte tag matches
  the new session can acknowledge a packet that was not passed up (see Limits).

The layer does not check either condition. Sixteen-bit sequence numbers are the default, and they wrap after 65536 packets of one session. Eight-bit numbers (`SequenceBits(8)`) wrap after 256 packets, so they are for paths that keep the order only. On such a path a copy of a packet that the path held for a whole session is refused by the session rules (see Sessions), and a copy of a packet that is behind the window is not passed up (see Receiving).

## Header

The first byte is the flags. The fields that follow are named by the flags, in this order:

| Flag | Value | Meaning |
|---|---|---|
| sequenced | 0x01 | a tag, a sequence number and a window base delta follow; the packet is a data packet or a sync |
| acknowledges | 0x02 | a tag and an acknowledgement number follow |
| sync | 0x04 | with sequenced: the packet starts a session; it has the boot value and the session counter after the delta, its sequence number is 0, its delta is 0, and it has no payload |
| resync | 0x08 | the byte that follows is the tag of the session the receiver asks to be started again; the packet is those two bytes |
| control | 0x10 | with acknowledges: a bare acknowledgement, with no tag-and-delta part and no payload; with resync: a counter reply (see Sessions) |

Each field, in order:

| Field | Size | Present when | Meaning |
|---|---|---|---|
| flags | 1 | always | the flags above |
| tag | 1 | sequenced, or resync, or acknowledges | the session tag (see Sessions); for resync, the tag of the session asked for |
| sequence | 2, or 1 with `SequenceBits(8)` | sequenced | the packet's number, little-endian |
| delta | 1 | sequenced | the sender's window base subtracted from the number: the number minus the oldest packet the sender has not settled |
| boot value | 2 | sync | the boot value of the sender, little-endian |
| counter | 1 | sync | the session counter of the sender's boot |
| ack tag | 1 | acknowledges | the tag of the session of the acknowledged packet |
| ack number | 2, or 1 with `SequenceBits(8)` | acknowledges | the acknowledged number, little-endian |
| held boot value | 2 | counter reply | the boot value of the session the receiver holds, little-endian |
| held counter | 1 | counter reply | the session counter of the session the receiver holds |

The header is at most 11 bytes: a sync with an acknowledgement is flags, tag, number, delta, boot value, counter, ack tag and ack number. Sequence fields are 1 byte with `SequenceBits(8)`.

The packets that are valid, by their flags:

| Flags | Packet | Bytes after the flags |
|---|---|---|
| 0x00 | data, not sequenced, not acknowledged | payload |
| 0x02 | data, not sequenced, with an acknowledgement | ack tag, ack number, payload |
| 0x01 | data | tag, number, delta, payload |
| 0x03 | data, with an acknowledgement | tag, number, delta, ack tag, ack number, payload |
| 0x05 | sync: the first packet of a session | tag, number 0, delta 0, boot value, counter; no payload |
| 0x07 | sync, with an acknowledgement | tag, number 0, delta 0, boot value, counter, ack tag, ack number; no payload |
| 0x08 | resync request | tag |
| 0x12 | bare acknowledgement | ack tag, ack number |
| 0x18 | counter reply | held boot value, held counter |

Any other packet is refused and counted as a layer error. So is a packet whose length does not match its flags, a sync with a payload, a sync whose number or delta is not 0, and a sync whose tag is not the tag of its boot value and counter (see Sessions).

Examples, with 16-bit numbers, the boot value 0x1234 given with `SetSession`, the session counter 0, and so the session tag is 0x34 (0x00 + 0x34):

```
sync:                                          05 34 00 00 00 34 12 00
sync of boot 0x5656, counter 0, acknowledging
  the packet 0x0001 of tag 0x34 (flags 0x07):  07 56 00 00 00 56 56 00 34 01 00
first data packet after the sync is
  acknowledged, number 0x0001, delta 0,
  payload 11 00 22:                            01 34 01 00 00 11 00 22
second data packet, sent while the first is
  unacknowledged, number 0x0002, delta 0x01,
  payload 33 00:                               01 34 02 00 01 33 00
bare acknowledgement of the sync:              12 34 00 00
resync request for tag 0x34:                   08 34
counter reply of boot 0x5656 holding
  counter 2:                                   18 56 56 02
data with payload CD 00, number 0x0001,
  delta 0, and the acknowledgement of 0x0002
  (tag 0x34):                                  03 34 01 00 00 34 02 00 CD 00
```

With `SequenceBits(8)` the same sync is `05 34 00 00 34 12 00` (7 bytes) and the bare acknowledgement of it is `12 34 00` (3 bytes).

Each of these bytes is checked by a test of the sim suite, written by hand from this table.

## Sessions

A session is one run of sequence numbers from one sender. A session starts with a sync, and the sender's data waits until the sync is acknowledged.

The sender's values:

- The boot value is the value given to `SetSession(value)`. It is required: until it is given, `Send` returns `noInit` and no session starts. It must differ for each boot. Where it comes from: a random number from the board's RNG adapter, the chip's unique id mixed with a boot counter kept in flash, or `std::random_device` on a PC. The same value on two boots breaks delivery (see Limits). `SetSession` applies to the sessions that start after the call.
- The session counter is 0 for the first session of a boot. Each new session of the same boot takes the next counter, one byte, so it wraps after 256 sessions.
- The tag is the counter plus the low byte of the boot value, one byte. Consecutive sessions of one boot have different tags for 256 sessions.
- A session's sync has the number 0 and the delta 0. The data of the session starts at number 1.

A session starts when the application sends, once `SetSession` has been called. The first `Send` after that returns `busy`, and it starts the session: the sync goes out at the next `Process`. A `Send` before `SetSession` returns `noInit`, sends nothing, and starts nothing. The sync is a control packet of its own. It carries no application data, so it has no outcome. `Send` returns `busy` until the sync is acknowledged. A session therefore costs one round trip at its start.

A sync that is sent again after it got no answer keeps its counter. So does a sync that is sent again after the sender gave it up: the sender's next `Send` sends the sync with the same counter.

The receiver's rules for a sync, with the boot value and counter it holds (the held session) and the tag of the sync:

| The sync | Result |
|---|---|
| another boot value | opens its session: the sender rebooted. The restart handler runs. |
| the held boot value, counter 1 to 127 ahead of the held counter (serial arithmetic) | opens its session. The restart handler runs when a session was held. |
| the held boot value and the held counter | a copy of the held session's sync: it is acknowledged again. Nothing else. |
| the held boot value, counter 128 to 255 ahead of the held counter (an older counter) | stale: it is not acknowledged and nothing else changes. The receiver sends a counter reply (0x18) with the held boot value and counter. |
| no session held | opens its session. No restart runs. |

A sync whose tag is not the tag of its boot value and counter is refused as a layer error.

The receiver's rules for the sequenced packets and the acknowledgements of the other sessions:

- A sequenced packet with a tag other than the held session's tag is not passed up, does not move the window and is not acknowledged. The receiver asks for the session of that tag with a resync request (0x08), which carries the tag. A receiver with no session does the same for every sequenced packet.
- An acknowledgement with a tag other than the sender's current tag is ignored.

The sender's rules for a resync request:

- A resync request is acted on only when its tag is the sender's current tag and the sender's session is established (its sync is acknowledged). The packets waiting for acknowledgement get the outcome `reset`, and they are not sent again. The restart handler runs. The next `Send` starts a session with the next counter.
- A resync request for another tag is ignored. So is one that arrives while the sync is still waiting for its acknowledgement: the sync gives the peer its session.

The sender's rules for a counter reply, which the receiver sends for a stale sync:

- A counter reply is acted on only when its boot value is the sender's boot value, the sender has no established session, and its held counter is not behind the sender's counter (serial arithmetic). The sync in flight is dropped, and the next session takes the counter after the held one. The receiver then takes that sync as newer.
- An established sender ignores a counter reply: its live session is not disturbed by a late copy of an old sync. A reply for another boot value is ignored too.

So a sender whose counter is behind the held counter of its boot value recovers: its first sync is stale to the receiver, the counter reply says which counter to start after, and its next sync opens the session. This does not make a repeated boot value safe (see Limits).

A sync is resent like any packet. When it has used its tries, it is dropped without an outcome, and the session is not established. The next `Send` starts it again with the same counter, so `Send` is busy for as long as the peer does not acknowledge the sync.

## Acknowledgements

Each sequenced packet is acknowledged by its own number and its tag. An acknowledgement is taken only when its tag is the tag of the sender's current session. An acknowledgement of an earlier session, with the number of a packet in flight, does not acknowledge that packet.

An acknowledgement goes on a packet that is sent to the peer in the meantime (its flags then include 0x02). Its tag is the tag of the peer's session that the number belongs to, which is not always the tag of the packet. When no packet is sent, the acknowledgement goes alone, at the next `Process`. A packet with the no-acknowledgement flag (`VubMeta::NoAcknowledgeFlag`) has no sequence number, no tag and no delta, and it is not acknowledged.

A duplicate of a packet is acknowledged again, because the first acknowledgement may be the one that was lost. It is not delivered again. With the in-order option a copy of a packet that is still held is not acknowledged; the packet is acknowledged when it goes up (see In order).

## Sending

`Send` returns `busy` while the session is starting, when the window is full, or when the new packet would lie a window or more after an unacknowledged one. The packets the sender has unsettled therefore always fit in one window, so the receiver's window covers them.

The window base of a packet is the oldest packet the sender has not settled: the oldest unacknowledged and not given up packet, or the next number when none is unsettled. Each sequenced packet carries its delta from that base, which is smaller than the window. A resend is built again with the base of its time, so it carries the base as it is then.

The first transmission is made with `Send`. A packet not acknowledged is resent when its timeout passes. The timeout before the first resend is `TimeoutMs`. After each resend the timeout doubles, up to `BackoffMaxMs`; with 0 the timeout stays the same.

A packet is sent at most `Retries + 1` times. When the timeout passes after the last one, the outcome is `retryExhausted`. With `Retries(3)` and `TimeoutMs(200)`, a packet sent at time 0 is sent again at 200, 400 and 600 ms, and reported at 800 ms.

While the connection is down (a link that reports its state, see the link state of the core) nothing is sent and no timeout runs. On up, every packet that is waiting is due at once and is resent in the next `Process`.

The timers compare the clock with wrap: the clock is a 32-bit count of microseconds, and a timeout that falls after the count wraps to zero is due at its time. Sim test `TestVubReliableClock` runs a session across the wrap.

## Receiving

The receiver's window starts at its base, the lowest number it has not received, and holds `Window` numbers.

Each sequenced packet of the held session first moves the receiver's base to the sender's base (the packet's number minus its delta), when that base is newer. The base moves only forward, and only to the sender's base: the numbers before it are settled at the sender, so they are acknowledged or given up. A packet from far ahead moves the base to the sender's base and no further, so it does not slide the window over numbers the sender still has.

Then the packet is checked against the window:

- A packet inside the window, `[base, base + Window)`, that is not a duplicate is passed up at once. The packets are delivered in the order they arrive. Packets are not held for a gap, unless the in-order option is on.
- A duplicate inside the window is acknowledged again and is not passed up.
- A packet behind the base by 1 to `Window` numbers is acknowledged again and is not passed up. A sender whose acknowledgement was lost settles its packet this way.
- A packet outside the window and not behind it is not passed up and not acknowledged. The sender resends it when its window has moved.

A packet behind the base by more than the window is not acknowledged and not passed up.

## Outcomes

The outcome handler (`OnOutcome`) is called once for each data packet sent with delivery, from `Process`:

| Status | Meaning |
|---|---|
| `ok` | the peer acknowledged the packet, and it was passed up |
| `retryExhausted` | no acknowledgement came after the last resend. The sender got no acknowledgement, so the packet may or may not have been passed up by the receiver. It is not sent again. |
| `reset` | the peer lost the session before the packet was acknowledged; it may or may not have arrived, and it is not sent again |

A `retryExhausted` packet is not a promise that it was not passed up. When every acknowledgement is lost, the receiver passes the packet up and the sender still reports `retryExhausted`. The same holds for `reset`. Only `ok` is a promise, and it is a promise that the packet was passed up once.

The handler gets the sequence number, the number of transmissions, and the payload. The payload is valid only during the call. `Send` returns a status and no token, so the application identifies its packets by their content, such as an id in the payload.

A send that returns an error (`busy`, `messageTooLong`) did not take the packet, and it has no outcome. A packet sent with the no-acknowledgement flag has no outcome: it is sent and forgotten. A sync has no outcome. Without an outcome handler, outcomes are not reported.

The restart handler (`OnRestart`) is called when the peer lost the session, on the side that sees it: the sender when its packets are reset, and the receiver when a sync opens a session over a held one or of another boot.

## Application: the session value through the stack

The application reaches the layer of a stack with `Find<Block>()` and sets its boot value once per boot, before the first send:

```cpp
// The application gives the board's layer its session value once per boot, before the first send. The layer is reached with Find<Block>().
// The value differs for each boot: a random number, or the chip's identifier mixed with a boot counter.
void BoardStartSession(uint16 bootValue) {
	board.Find<VubReliable>().SetSession(bootValue);
}
```

The value must differ for each boot. The sources, in the order a board would use them:

- a random number from the board's RNG adapter, drawn at each boot;
- the chip's unique id mixed with a boot counter that is kept in flash and counted up at each boot;
- `std::random_device` on a PC.

The layer cannot check the value. If two boots give the same value, the second boot's sessions match the first boot's: the receiver takes the sync as a copy of a session it holds, or as a counter it has already passed, and the sender's packets can then be acknowledged as duplicates, not passed up, and reported `ok`, or the sender stays `busy`. The counter reply (see Sessions) recovers some of these cases, not all. Sim test `TestVubReliableWiki` runs this call on a chain with the same builder lines and a link that keeps the first packet it sends. With the boot value 0x5A5A the sync is `05 5A 00 00 00 5A 5A 00`.

## Options

The options are set by the methods right after the block. Each has a default.

| Method | Meaning | Default |
|---|---|---|
| `Retries(n)` | resends before `retryExhausted`; 0 is a compile error with the in-order option (an options value built by hand is not checked) | 3 |
| `TimeoutMs(ms)` | timeout before the first resend, in milliseconds | 200 |
| `Window(n)` | packets unsettled at once: 1 to `WindowCapacity`, and at most 32 | 4, or `WindowCapacity` when that is smaller |
| `SequenceBits(bits)` | width of the sequence and acknowledgement numbers: 8 or 16. Use 8 only on a path that keeps the send order. | 16 |
| `BackoffMaxMs(ms)` | the timeout doubles after each try, up to this cap; 0 keeps it fixed | 0 |
| `PayloadBytes(n)` | the largest payload; larger payloads are refused with `messageTooLong` | `PayloadCapacity` |
| `OnOutcome<&Fn>()` | the outcome handler | none |
| `OnRestart<&Fn>()` | the restart handler | none |

A value out of range is a compile error: `TimeoutMs(0)`, `Window` above `WindowCapacity` or 32, `SequenceBits` other than 8 or 16, `PayloadBytes` above `PayloadCapacity`, and `Retries(0)` with the in-order option (see In order). Options are set in the chain, for example `.Layer<VubReliable>().Retries(5).TimeoutMs(100).Window(2).BackoffMaxMs(800)`.

The in-order option is not a method: it is the third template argument (`.Layer<VubReliableOf<40, 4, true>>()`). See In order.

## In order

With `VubReliableOf<PayloadCapacity, WindowCapacity, true>` the packets go up in the order of their numbers. The wire format and the sender are the same as without the option; only the receiver changes.

- A data packet inside the window is held until the packets before it have gone up. A packet that arrives ahead of a gap is not passed up at once.
- Held packets go up from `Process`, through the outbox's `Deliver` (the layer declares `passesUp`). Each one goes up once, in sequence order.
- A held packet is acknowledged when it goes up. So an acknowledgement means the packet was passed up, and an outcome of `ok` means the same. A copy of a held packet is neither acknowledged nor kept again.
- The gap fills: the held packets after it go up in order, and each is acknowledged.
- The sender gives up on a gap (`retryExhausted`): the sender's next data packet carries its new window base in the delta, and the receiver then passes the held packets up past the gap. The numbers below the sender's base are not passed up, so the gap packets are not passed up later.
- A packet acknowledged after a give-up of a packet before it can carry `retryExhausted`: it was held, and it went up after the sender had stopped waiting for it. This is allowed by the outcome table.
- A receiver reboot clears the held packets with its session. The sender's resends of them are refused by the new receiver, which asks for a resync, and the sender reports them `reset`.
- A payload larger than `PayloadCapacity` is not held and not acknowledged.
- The option is off by default. With it off the layer has no held store: its type is the same as the plain layer's, and the code of the option is not instantiated.

Limits of the option:

- The window moves in `Process`. A packet that arrives after the sender gave up on a gap is refused while the receiver still has the gap, unless the sender sends it again. The sender sends it again only with `Retries` of 1 or more. `Retries(0)` with this option is a compile error. A held packet is acknowledged only when it goes up, so with no retry each held packet is given up at its first timeout, before the gap in front of it fills. The model check of the option with `Retries(0)` fails that way: once the faults stop, packets sent after them are not reported `ok`. Use at least one retry on an in-order path.
- A packet held behind a gap waits for the next packet of the sender, which carries the new window base. When the sender sends nothing more after giving up on a gap, the held packets wait: they are passed up by the next packet of the sender, and until then they are not acknowledged, so the sender reports them `retryExhausted`.

Storage: each window slot holds one payload (`PayloadCapacity` bytes), and there are `WindowCapacity` slots, plus a length of 2 bytes per slot, a 2-byte settled base and a 1-byte head. Measured in the storage section below: 168 bytes for `VubReliableOf<40, 4, true>` over `VubReliableOf<40, 4>`, and 336 bytes for `VubReliableOf<40, 8, true>` over `VubReliableOf<40, 8>`.

Sim test `TestVubReliableInOrder` checks the option: the packets of a faulty line come up in order and each once, a gap that the sender gives up releases the packets after it, a receiver reboot clears the held packets, a copy of a held packet is not acknowledged, eight-bit numbers wrap over 300 packets, and a payload over the capacity is not held. Its chain is the chain of the faulty line with `VubReliableOf<40, 4, true>`.

## Storage and cost

The storage depends on the template arguments, not on the options. Each window slot holds one packet (`PayloadCapacity` plus the 11-byte header area) and its state. The owed acknowledgements take 3 bytes for each of the `2 * WindowCapacity` entries (a number and its tag, 2 bytes plus 1). The receiver keeps 4 bytes of bits for its window. The session fields (the boot value, the counters, the held boot value and the counter-reply flag) fit in the padding of the state.

Sizes, measured with arm-none-eabi-g++ 15.2.1 for Cortex-M4 (`-std=c++20 -Os -mcpu=cortex-m4 -mthumb -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti`). The method: one global array per layer type, sized with `sizeof` of that type, and the size of its symbol in the object file (`arm-none-eabi-nm -S`):

| Layer | `sizeof` |
|---|---|
| `VubReliableOf<32, 4>` | 336 bytes |
| `VubReliableOf<40, 4>` | 368 bytes |
| `VubReliableOf<40, 8>` | 664 bytes |
| `VubReliableOf<40, 4, true>` | 536 bytes (+168) |
| `VubReliableOf<40, 8, true>` | 1000 bytes (+336) |

The sizes of the layers without the option are the same as the table of the previous version: the counter reply adds no bytes. The growth in the previous version came from the header area of each slot, from 7 to 11 bytes: 4 bytes for each slot.

The text of the layer is measured on a unit that constructs the layer with options passed in from another unit (so the options are not folded), calls `SetSession`, `Process`, `Add`, `Process` and `Take`, with the layer's type given by the in-order flag. The text of the unit, with no data and no bss:

- the unit with the plain layer, `VubReliableOf<40, 4>`: 1698 bytes of text;
- the unit with `VubReliableOf<40, 4, true>`: 1962 bytes of text (+264).

The in-order code exists only in the unit with the option: the plain unit's object has no symbol of `ReleaseInOrder`, `HoldPacket` or `ShiftBase`, and the in-order unit's object has `ShiftBase` as a separate function. The counter reply is protocol, not an option, so its code is in every unit; its text is not measured on its own here. The figures are the sum of the `.text` sections of each object file (`arm-none-eabi-size -A`). In the in-order unit `PushOwed` and `ShiftBase` are separate functions; the rest of the in-order code is inlined into `Process` and the unit's own code.

The options that are off are not removed at compile time. The layer holds its options as values, given to it at run time, so its checks stay in the code. A compile-time null would need the handler in the layer's type, and the builder does not give the layer a type that carries it in this version.

The cost of the run-time options is measured on configuration (i) of the size table (COBS, CRC-8, window 1, Retries 3). Its options set the window, the retries and the timeout. The sequence width (16 bits by default), the backoff and the two handlers are left at their defaults, but their code stays in the image. A copy of the layer's header in which the sequence width is fixed to 16 bits, the backoff to 0 and both handlers to none gives an image 324 bytes smaller on the Cortex-M4 (3952 against 4276 bytes of text, linked with `--gc-sections` and read with `arm-none-eabi-size`, the same flags as the size script). Removing that code needs the options as template arguments, which this version of the builder does not pass.

With `WindowCapacity` 1 the window is 1, and the layer has no code for the packets ahead of its base: its default is 1, the receiving side keeps no bits for packets ahead, and the sender does not search the window for a span. The in-order option keeps its bit for the packet at the base. An options value built by hand with a window above 1 is not supported with `WindowCapacity` 1: a packet ahead of the base is then not acknowledged and not passed up.

The out-of-line functions of the layer in configuration (i), on the Cortex-M4 (`arm-none-eabi-nm --size-sort -S -C` on the linked image, bytes of text):

| Function | Bytes | What it serves |
|---|---:|---|
| `Process` | 286 | the sync start, the resync request and the counter reply that are owed, the outcomes, the timers and the owed acknowledgements |
| `Transmit` | 200 | a resend of a packet or of its sync, with an owed acknowledgement that rides on it |
| `RunTimers` | 134 | the due resends, retryExhausted, and the sync given up without an outcome |
| `PutHeader` | 122 | the header of every packet: the sequence fields, the sync fields and the acknowledgement fields, written once |
| `FlushAcknowledgements` | 114 | the owed acknowledgements that did not ride on a packet |
| `Settle` | 66 | the outcome report and the release of a slot |
| `SenderBase` | 38 | the window base that a packet's delta is taken from |
| `ReceiveAcknowledgement` | 38 | an acknowledgement of the current session; the acknowledgement of a sync establishes the session |
| `PushOwed` | 38 | the ring of owed acknowledgements |
| `PopOwed` | 18 | the same ring, when an acknowledgement is sent |
| `CopyBytes` | 18 | the byte copies of the packet and the slot |
| `SyncInFlight` | 16 | whether a sync is waiting for its acknowledgement |

The in-order functions (`HoldPacket`, `ReleaseInOrder`, `ShiftBase`) have no symbol in configuration (i), since the option is off. The receiving path (the take of a packet, the session and the window) has no symbol of its own in the image: it is inlined into the stack's functions.

## Limits

- The tag is one byte. Sessions of different boots can have the same tag: about 1 in 256 for each pair of sessions. On an order-keeping path a packet of an earlier boot cannot arrive after the sync of a later one, so this does not reach the data. On a reordering path a packet of an earlier session that carries the tag of the current session is taken as part of the current session. The same holds for an acknowledgement: an acknowledgement of an earlier session with the tag and number of a packet in flight acknowledges that packet, so the sender can report `ok` for a packet that was not passed up. This needs two boots with the same tag, and an acknowledgement of the earlier boot that is delayed past the start of the later boot's session.
- The layer cannot tell a boot value that repeats from a fresh one. A sender that restarts with a boot value the receiver already holds can see its data acknowledged as duplicates and reported `ok` without being passed up, or see `busy` for as long as the receiver holds the session. The value must differ for each boot (see Application).
- A receiver that reboots after delivering a packet and before acknowledging it cannot know that it did. It has no session, so the sender's resend is refused and a resync request comes back. The sender reports the packet `reset`, never `ok`, and does not send it again. The packet may have been delivered once, by the receiver before the reboot. The sync carries no data, so a reboot after the sync's acknowledgement delivers nothing twice.
- A sync that gets no acknowledgement gives `busy` from `Send` for as long as it is unanswered, and no outcome. The application learns of the peer's absence only from the `busy` answers.
- A receiver that holds a session takes a sync as newer only when its counter is 1 to 127 ahead. The sender's counter moves by one for each new session, so after an accepted session the next one is one ahead. A receiver that has no session takes any counter. A sync that is behind the held counter is answered with a counter reply, not taken as newer.
- In order: see the limits of the option in In order.

## Not in this version

- Addressing: the layer keeps the state of one peer. The state is one struct, so a table of peers is a later stage.
