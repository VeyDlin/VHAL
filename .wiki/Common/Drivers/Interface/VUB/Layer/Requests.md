# VUB::Requests

The requests layer: a request with a typed body and one final reply, interim replies, a command with no reply, and the handlers of the
commands a device serves. A request is sent by the requester's `Send`; its handler runs on the responder, from the stack's `Process` in the
walk that takes the request. The layer sits above `VUB::Reliable` for delivery, or directly above a framing or a link; its packets are the
payloads of the block below.

The layer does not build cancel, events, ping and health, or `Await` (see Not built).

The layer is added with `.Layer<VUB::Requests<Commands, Handlers>>()`, where `Commands` is a `VUB::CommandSet` (see Requests/Commands) and
`Handlers` a `VUB::Handlers` list (see Requests/Handlers). Its options follow it as Step methods, and each builds only what it sizes:

- `.Open(n)`: `n` requester slots, the number of requests open at once. `Open(0)` (the default) builds no requester slot.
- `.Tickets(n)`: `n` responder tickets, the number of requests the device holds for its handlers at once. A served command with a reply
  needs at least one (a compile error otherwise). A handler set with no command with a reply needs none.
- `.Refusals(n)`: `n` entries in the refusal ring (default 4), the refusals waiting to be sent from `Process`.

```cpp
using DeviceLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<MotorCommands, DeviceHandlers>>().Tickets(4)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;
```

The layer's `Requests()` handle is the application's entry (see Calls). The layer needs a clock (`Clock<&Fn>()`), and the Reliable layer below it
needs its session value (`SetSession`, see Reliable).

## Files

- `Layer/Requests.h`: the options, the Step methods, the handle, and `Take`, `Process` and the delivery hooks. It composes the two parts.
- `Requests/Requester.h` (`RequesterOf`): the requester part. It holds the slots, the numbers and tags, the link state, and the calls
  `Start`, `Fire`, the takes of a final and an interim, the deadlines, the outcomes and the restart.
- `Requests/Responder.h` (`ResponderOf`): the responder part. It holds the tickets and the refusal ring, and the calls of the received requests,
  the finals, the interims, the deadlines and the restart.
- `Requests/Ticket.h`: `RequestId`, `TicketOps` and `Ticket`. `Requests/Wire.h`: the wire kinds, the codes and their results, and the
  deadline compare.
- Tests: `TestVubRequestsWire.cpp` (the bytes), `TestVubRequestsRequester.cpp`, `TestVubRequestsResponder.cpp` and `TestVubRequestsPath.cpp`
  (chains, links and restarts). Their command set and helpers are in `.tool/sim/RequestsChains.h`.

A part that is off has no storage: its member is an empty type.

## Below the layer

With `VUB::Reliable` directly below, the layer takes the owner of Reliable's outcomes and restarts (`wantsOwner`), so a request learns the
delivery of its packet, and a peer restart closes its requests. The builder sets the layer's option `ownerBelow` to true for that chain.

Without Reliable (a framing or a link directly below), the layer has no outcomes and no restarts, and the code for them is not built
(`ownerBelow` is false). A request then ends with its final, its deadline, or the loss of the connection for a `FailWhileDown` command.
A command with a reply is sent with `Send` as usual. A command with no reply has a delivery outcome, so it is sent only with Reliable below:
use `Fire` (see Calls) for no delivery at all.

## Build-time rules

Each rule is a `static_assert` or a failed check with the message shown.

| Rule | Message |
|---|---|
| A `Send` on a layer with no `Open(n)` | `Send needs requester slots: give the layer Open(n) with n of at least 1` |
| A `Send` of a command with no reply (`NoReply()`) and no Reliable below | `a command with a delivery outcome needs Reliable below Requests; use .Fire() for no delivery at all` |
| A `Fire` of a command with a reply | `Fire takes a command with no reply: a command with a reply is sent with Send` |
| A `Send` of a command with a reply and no `ReplyTimeoutMs` | `a command with a reply needs ReplyTimeoutMs: the requester's deadline` |
| A served command with a reply, and `Tickets(0)` (checked when the layer takes a packet) | `the layer serves a command and has no tickets: give Tickets(n) with n of at least 1` |
| `Ticket::Interim` on a command without `.Interim()` | `Ticket::Interim needs the command to be declared Interim(): add .Interim() after its Reply` |
| `ReplyTimeoutMs` or `TicketTimeoutMs` above 2147483 | `ReplyTimeoutMs must be at most 2147483 (the deadlines are signed 32-bit microsecond counts)` |
| A handler whose signature does not match its command (`CheckHandlers`, `CheckFinal`, `CheckProgress`) | the messages of Requests/Handlers |

## Calls

`link.Requests()` gives the handle: a `Send` for a command with a reply or with `NoReply()`, and a `Fire` for a command declared `Fire()`.
The handle is built on the layer and an outbox, so a call sends at once on a stack that is not concurrent.

```cpp
// A request of a command with a reply: the final and the interim replies go to the two handlers (the interim one is optional).
auto sent = link.Requests().Send<"motor.start", &OnStartFinal, &OnStartInterim>(MotorStart{1200, 1});

// A request of a command with no reply: no slot, no outcome, no handler.
link.Requests().Fire<"motor.reset">(VUB::NoBody{});
```

`Send` returns `Result<RequestId>`. The `RequestId` is the request's number (never 0). Errors come back at once, with nothing sent and no
number used: `busy` (no free slot, or the layer below refuses the packet), `noConnection` (the command is `FailWhileDown` and the connection
is down, or the link has not come up yet), `noInit` (Reliable has no session yet). A `Fire` gives the same results, except the slot.

The final handler is `void (*)(const VUB::Final<Status, Reply>&)` for a command with a reply, and `void (*)(ResultStatus)` for a command declared
with `NoReply()`, whose answer is the delivery outcome. A `Final` has `result` (`ok` for a reply that arrived; otherwise the local or refusal
result), `status` and `body`. The interim handler is `void (*)(const VUB::Progress<Status>&)`.

When the handlers run:

- A final or an interim that arrives on the wire runs its handler inside the walk that takes the packet, so the handler can send at once. The
  slot of a final is freed before its handler runs, so the handler can send the next request into it.
- A deadline, a delivery outcome, the loss of the connection, and a peer restart set the request's result. Its handler runs once, in the next
  `Process`, from the layer's own context, not from inside Reliable.
- Each request's final handler runs once. Each interim reply runs its handler once, in arrival order, and none is lost: ten interims in one
  walk run ten handlers.

## Requester

A request takes a free slot and the next number. Numbers start at 1, skip 0 and any number that an open request has, and wrap after 65535.
Each request also has a tag, one byte from its own counter, which advances with the number. The tag and the number together identify a
request, so a late packet of an earlier request with a reused number is dropped. A refused `Send` uses neither the slot nor a number, so the
number that follows is the next one.

A request's end is its first result: its final, its deadline, a Reliable outcome of `retryExhausted` or `reset`, or `noConnection`. A
`FailWhileDown` command is closed with `noConnection` when the connection goes down, and a `Send` of it while the connection is down, or while
the link has not come up yet, returns `noConnection` at once. The other commands wait for the connection, and their deadlines run. A request
that Reliable reports `reset` or `retryExhausted` may or may not have run on the device, and its final is not waited for.

Each interim reply re-arms the request's deadline. An interim is dropped when its index is not greater than the last accepted one (so the
replies are in order over a non-in-order Reliable), or when its status is a reserved value (0xF0 or more). Interims are not queued: each one is
handed to its handler when it is taken.

A peer restart (Reliable reports it through its owner hook) closes every open request with `reset`.

Reliable asks whether a request packet is still wanted before it sends the packet again (`DeliveryWanted`). A request is wanted while its
slot is open. A request that its requester has closed, by its deadline or by the connection's loss, is not sent again when the link comes back,
and the device's handler does not run for it. A packet that the stack has already queued for the link is sent when the link is up again, and it
is not asked about: under `Down::Wait` the stack keeps its queued packets through an outage, so such a request can still reach the device after
its requester has closed it. Under `Down::Fail` the queued packets are dropped, and the request's resend is then the one that is asked about.

## Responder

A request taken from the wire is checked and copied in `Take`, because the packet is valid only during the walk.

- A command with a reply takes a ticket. Its handler runs from `Process` in arrival order, with the ticket and the decoded body:
  `void(VUB::Ticket<Commands, Name>, const Body&)`, or `void(Ticket)` without a body.
- A command with `NoReply()` or `Fire()` takes no ticket, so a ticket being held does not refuse it. Its handler runs at once, in `Take`,
  with the decoded body: `void(const Body&)`, or `void()` without a body. A request of a command with a reply that arrived earlier in the same
  walk runs its handler first, so the handlers run in arrival order.
- A bad body of a command with no reply is returned from `Take` as `invalidData`, so the stack counts it in its layer errors, and the handler
  does not run.

The ticket's operations:

- `Interim(status)` sends an interim reply. It is a compile error for a command that is not declared `Interim()`. It returns `ok`, `busy`
  while the last interim is unsent, `invalidParameter` after 255 interims, and `notAvailable` once the final is made.
- `Finish(status)` or `Finish(status, body)` makes the final: `ok`, or `notAvailable` when the ticket is closed.
- `Open()` is true while no final is made for the request.
- `Number()` is the request's number.

Interims are on the responder only when the command set has a command declared `Interim()`. A set with none has no interim code and no
interim fields in its tickets.

A final made in a handler goes out in the same `Process`, after the handler's interims. A final made later goes at the next `Process`; a
final that Reliable refuses as busy is kept in the ticket and sent again. A ticket that is never finished ends with its `TicketTimeoutMs` and
a final with code 5 (`timeout`). An interim re-arms that deadline.

A request whose number is open on the responder is handled by its tag: the same tag is a duplicate and is ignored; another tag is refused with
busy. A peer restart closes every ticket, and the final of an open ticket is discarded.

## Refusals

A request the layer does not admit is answered by a final from the refusal ring, decided in `Take` and sent from `Process`. The codes are the
fixed codes of the wire (below): 2 for an unknown command id or a command with no handler, 3 for a body that is too short or has a bad bool,
and 4 (busy) for no free ticket. A refused command with no reply gets no answer. A refusal that finds the ring full is dropped and the
requester's deadline ends the request.

## Wire

The layer's packets are the payloads of the block below. The kind is byte 0. Numbers and tags are as above; all multi-byte fields are
little-endian.

| Kind | Layout | Packet |
|---|---|---|
| 0x01 REQUEST | `01 id(1) number(2) tag(1) body` | from the requester |
| 0x02 INTERIM | `02 number(2) tag(1) index(1) status(1)` | from the responder |
| 0x03 FINAL | `03 number(2) tag(1) code(1)`, and for code 0: `status(1) body` | from the responder |

The FINAL codes are a fixed table of the layer, not the core's `ResultStatus` numbers:

| Code | Result | Sent for |
|---|---|---|
| 0 | ok (`status` and the body follow) | the handler's `Finish` |
| 1 | operationCancelled | a cancel (not built here) |
| 2 | notSupported | an unknown command id, or no handler |
| 3 | invalidData | a body too short, or a bad bool |
| 4 | busy | no free ticket |
| 5 | timeout | the responder's ticket deadline |
| 6 | invalidParameter | reserved; not sent in this version |
| 7 to 255 | not used | a requester reads one as invalidData |

Ids 0xF0 to 0xFF and statuses 0xF0 to 0xFF are the layer's: a command or event id of that range is a compile error, and an interim or final
status of that range is refused.

The byte layouts of design section 5.6 are the layer's tests (`TestVubRequestsWire.cpp`): a request of `motor.start` with number 7 and
`MotorStart{1200, 1}` is `01 12 07 00 07 B0 04 01 2C 01`; its interim `warming` is `02 07 00 07 01 01`; its final with the report is
`03 07 00 07 00 00 B0 04 52 03`; a refusal for an unknown id 0x77 with number 11 is `03 0B 00 0B 02`. Over a chain without Reliable the
request is the bare packet: `motor.stop` with number 1 and tag 1 is `01 13 01 00 01`, and its reply is `03 01 00 01 00 00`.

## Failure paths

| Case | Requester sees | Responder does |
|---|---|---|
| The final is answered in the same round as the request | `ok` | one packet back, with the acknowledgement of the request |
| The device holds the request too long | `timeout` at the requester's deadline; the device's own deadline closes the ticket with code 5 | the ticket closes; a later `Finish` is `notAvailable` |
| The request is lost and Reliable gives up | `retryExhausted` | the ticket may run; its final is dropped at the requester |
| The request is lost, the link goes down, and the deadline passes while it is down | `timeout`; the resend is not sent (`DeliveryWanted`) | the handler does not run |
| The peer restarts with a request open | `reset` (both open requests, when the requester sends again) | the tickets are dropped |
| The link goes down | `noConnection` for `FailWhileDown` commands; the others wait for their deadline | nothing is sent while down |
| The link has not come up yet | `noConnection` for `FailWhileDown` commands | none |
| The requester's table is full | `busy` at once; no number used | none |
| No free ticket | `busy` (code 4) | a `Fire` or `NoReply` still runs its handler |
| A late or mismatched FINAL or INTERIM | dropped | none |
| A number that wraps while an earlier request with the same number is still open | the old request's final does not close the new one (tag) | none |

## Size

Measured for Cortex-M4 and M0+ on a packet link, `-Os`, with `arm-none-eabi-g++` 15.2 and `arm-none-eabi-size`. Each row is the difference
against one program with Reliable (32, 4) and the same link, the same calls and no requests layer. Text and RAM (data plus bss) are in bytes.

| Configuration | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| Responder only: one command with a reply, `Tickets(1)`, `Refusals(2)` | 1572 | 48 | 1564 | 48 |
| Requester only: one `Send` site, `Open(2)` | 1812 | 44 | 1848 | 44 |
| Both: `Open(2)`, `Tickets(1)`, `Refusals(2)`, one served command | 2640 | 80 | 2708 | 80 |

The layer's own storage, from `sizeof`: a requester of `Open(2)` is 40 bytes, or 52 bytes when its command set has an interim command; a
responder with `Tickets(2)` and `Refusals(2)` is 68 bytes, or 72 bytes with an interim command. The layer of a set with no interim command has
no interim fields.

## Not built

These are not in this layer, and the declarations for them are not accepted:

- Cancel (kind 0x05), the cancellable commands' cancel handlers, the cancelRefused interim (0xF8), and `CancelAll`.
- Events (kind 0x04): `Emit`, the event handlers, `Delivered()`.
- Ping (0xF0) and health (0xF1), with their counters.
- `Await` (RTOS) and `Concurrent()`. On a concurrent stack the handle refuses its sends with `notSupported`, because the layer then sends
  from its own `Process`.
