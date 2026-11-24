# VUB::Requests

The requests layer: a request with a typed body and one final reply, interim replies, a command with no reply, and the handlers of the
commands a device serves. The layer sits above `VUB::Reliable` for delivery, or directly above a transform, a framing or a link; its packets
are the payloads of the block below.

The layer does not build cancel, events, ping and health, or `Await` (see Not built).

The example below is a device: it serves two commands and sends its packets over a Reliable layer. It is compiled by
`.tool/sim/TestVubRequestsLayerWiki.cpp`.

```cpp
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <span>


namespace Motor {

enum class MotorStatus : uint8 {
	started = 0,
	warming = 1
};


enum class MotorCommand : uint8 {
	start = 0x12,
	stop = 0x13
};


struct MotorStart {
	uint16 speedRpm = 0;
	uint8 direction = 0;
};


struct MotorReport {
	uint16 speedRpm = 0;
};

}


namespace VUB {

template<>
struct BodyOf<Motor::MotorStart> {
	static consteval auto Layout() {
		return Fields<Motor::MotorStart>()
			.Field<&Motor::MotorStart::speedRpm>()
			.Field<&Motor::MotorStart::direction>();
	}
};


template<>
struct BodyOf<Motor::MotorReport> {
	static consteval auto Layout() {
		return Fields<Motor::MotorReport>()
			.Field<&Motor::MotorReport::speedRpm>();
	}
};

}


namespace Motor {

using MotorCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<MotorCommand::start>()
			.Request<MotorStart>()
			.Reply<MotorStatus, MotorReport>()
			.Interim()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<MotorCommand::stop>()
			.Reply<MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000);
}>;


using StartTicket = VUB::Ticket<MotorCommands, MotorCommand::start>;
using StopTicket = VUB::Ticket<MotorCommands, MotorCommand::stop>;


void OnStart(StartTicket ticket, const MotorStart& body) {
	(void)body;
	(void)ticket.Interim(MotorStatus::warming);
	MotorReport report{};
	(void)ticket.Finish(MotorStatus::started, report);
}


void OnStop(StopTicket ticket) {
	(void)ticket.Finish(MotorStatus::started);
}


using DeviceHandlers = VUB::Handlers<
	VUB::On<MotorCommand::start, &OnStart>,
	VUB::On<MotorCommand::stop, &OnStop>>;

}


// The link is a stub here: a board puts its UART in its place. Write takes the bytes and reports them sent.
struct UartLink {
	static constexpr VUB::LinkTraits traits = VUB::LinkTraits().MaxPacket(64).HalfDuplex().Starts(VUB::Initiator::Either);


	Result<std::size_t> Write(std::span<const uint8> bytes) {
		return bytes.size();
	}
};


void HandlePacket(const VUB::Meta&, std::span<const uint8>) {
}


uint32 MicrosecondClock() {
	return 0;
}


using DeviceLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, Motor::DeviceHandlers>>()
			.Tickets(4)
		.Layer<VUB::ReliableOf<40, 4>>()
			.Retries(3)
			.TimeoutMs(200)
			.Window(4)
		.Transform<VUB::Crc16Modbus>()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;
```

The layer is added with `.Layer<VUB::Requests<Commands, Handlers>>()`, where `Commands` is a `VUB::CommandSet` (see Requests/Commands) and
`Handlers` a `VUB::Handlers` list. A command is named by a value of an `enum class` with an underlying type of `uint8`, and the value is its
id on the wire. Its options follow the layer as Step methods, and each builds only what it sizes:

- `.Open(n)`: `n` requester slots, the number of requests open at once. `Open(0)` (the default) builds no requester slot.
- `.Tickets(n)`: `n` responder tickets, the number of requests the device holds for its handlers at once. A served command with a reply
  needs at least one (a compile error otherwise). A handler list with no command with a reply needs none.
- `.Refusals(n)`: `n` entries in the refusal ring (default 4), the refusals waiting to be sent from `Process`.

The layer's `Requests()` handle is the application's entry (see Calls). The layer needs a clock (`Clock<&Fn>()`). The Reliable layer below it
needs its session value (`SetSession`, see Reliable).

## Files

- `Layer/Requests.h`: the options, the Step methods, the handle, and `Take`, `Process` and the delivery hooks. It composes the two parts.
- `Requests/Requester.h` (`RequesterOf`): the requester part. It holds the slots, the numbers and tags, the session, the link state, and the
  calls `Start`, `Fire`, the takes of a final and an interim, the deadlines, the outcomes and the restart.
- `Requests/Responder.h` (`ResponderOf`): the responder part. It holds the tickets and the refusal ring, and the calls of the received requests,
  the finals, the interims, the deadlines and the restart.
- `Requests/Events.h` (`EventsOf`): the emitter of the events and the receiver of the event packets. It holds no state.
- `Requests/Ticket.h`: `RequestId`, `TicketOps` and `Ticket`. `Requests/Wire.h`: the wire kinds, the codes and their results, and the
  deadline compare.
- Tests: `TestVubRequestsWire.cpp` (the bytes), `TestVubRequestsRequester.cpp`, `TestVubRequestsResponder.cpp`,
  `TestVubRequestsPath.cpp` (chains, links and restarts), `TestVubRequestsEvents.cpp` (events and counters) and
  `TestVubRequestsLayerWiki.cpp` (the example above). Their command sets and helpers are in `.tool/sim/RequestsChains.h` and
  `.tool/sim/RequestsMotor.h`.

A part that is off has no storage: its member is an empty type.

## Below the layer

With `VUB::Reliable` directly below, the layer takes the owner of Reliable's outcomes and restarts, so a request learns the delivery of its
packet, and a peer restart closes its requests. The builder sets the layer's option `ownerBelow` to true for that chain. The handle then has
no `SetSession`, and the layer has no session code.

Without Reliable (a transform, a framing or a link directly below), the layer has no outcomes and no restarts, and the code for them is not
built (`ownerBelow` is false). A request then ends with its final, its deadline, or the loss of the connection for a `FailWhileDown` command.
The session is then the requester's to give (see Requester). A command with a reply is sent with `Send` as usual. A command with no reply has
a delivery outcome, so it is sent only with Reliable below: use `Fire` (see Calls) for no delivery at all.

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
| A command id of 0xF0 or more | `a command id is 0xF0 or more: the ids 0xF0 to 0xFF are reserved for the layer` (an event id has the matching message) |
| A handler whose signature does not match its command (`CheckHandlers`, `CheckFinal`, `CheckProgress`) | the messages of the handler list checks, in `Requests/Handlers.h` |

## Calls

`link.Requests()` gives the handle: a `Send` for a command with a reply or with `NoReply()`, a `Fire` for a command declared `Fire()`, and
`SetSession` for a layer with no Reliable below. The calls below continue the example above, with its command and handlers and the final
handlers `OnStartFinal`, `OnStartInterim` and `OnStopFinal` declared by the application.

```cpp
// A request of a command with a reply: the final and the interim replies go to the two handlers (the interim one is optional).
auto sent = link.Requests().Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(Motor::MotorStart{1200, 1});

// A request of a command with a reply and no interim replies.
auto stopped = link.Requests().Send<Motor::MotorCommand::stop, &OnStopFinal>(VUB::NoBody{});
```

A command declared `Fire()` is sent with `Fire`: no slot, no outcome, and no handler. A command declared `NoReply()` is sent with `Send`, which
takes the outcome as its answer and needs Reliable below.

`Send` returns `Result<RequestId>`. The `RequestId` is the request's number (never 0). Errors come back at once, with nothing sent and no
number used: `busy` (no free slot, or the layer below refuses the packet), `noConnection` (the command is `FailWhileDown` and the connection
is down, or the link has not come up yet), `noInit` (Reliable has no session yet, or, with no Reliable below, `SetSession` has not been
called). A `Fire` gives the same results except `noInit`: it needs no session.

The final handler is `void (*)(const VUB::Final<Status, Reply>&)` for a command with a reply, and `void (*)(ResultStatus)` for a command declared
with `NoReply()`, whose answer is the delivery outcome. A `Final` has `result` (`ok` for a reply that arrived; otherwise the local or refusal
result), `status` and `body`. The interim handler is `void (*)(const VUB::Progress<Status>&)`.

## When the handlers run

- A final or an interim that arrives on the wire runs its handler inside the walk that takes the packet, so the handler can send at once. The
  slot of a final is freed before its handler runs, so the handler can send the next request into it.
- A ticket handler of a served command (`On`) runs from the layer's `Process`, in arrival order, and the final it makes goes out in the same
  `Process`.
- A command with no reply that arrives (its `Fire` or `NoReply` request) runs its handler inside the walk that takes it, after the requests
  taken earlier in that walk.
- A deadline, a delivery outcome, the loss of the connection, and a peer restart set the request's result. Its handler runs once, in the next
  `Process`, from the layer's own context, not from inside Reliable.
- Each request's final handler runs once. Each interim reply runs its handler once, in arrival order, and none is lost: ten interims in one
  walk run ten handlers.

## Requester

A request takes a free slot and the next number. The number counter skips 0 and any number that an open request has. It wraps after 65535.
Each request also has a tag, one byte from its own counter, which advances with the number. The tag and the number together identify a request,
so a late packet of an earlier request with a reused number is dropped. A refused `Send` uses
neither the slot nor a number, so the number that follows is the next one.

With no Reliable below, the requester needs a session value: `SetSession(value)` on the handle. Each boot gives a different value, for example
a stored counter or a random number. The first number is the low 16 bits of `value` × 0x9E37, and the first tag is the low byte of `value`.
Number 0 is never sent: a `value` of 0 gives number 1 first, and a tag of 0 is valid. The first numbers of values that follow each other are
far apart, so a request of an earlier boot that is still open on the responder is not taken for a request of this boot, and its final does not
close one. Until `SetSession` is called, `Send` returns `noInit` and nothing is sent. `Fire` does not need a session. With Reliable below there
is no `SetSession` on the handle; the session is Reliable's (see Reliable).

A request's end is its first result: its final, its deadline, a Reliable outcome of `retryExhausted` or `reset`, or `noConnection`. A
`FailWhileDown` command is closed with `noConnection` when the connection goes down, and a `Send` of it while the connection is down, or while
the link has not come up yet, returns `noConnection` at once. The other commands wait for the connection, and their deadlines run. A request
that Reliable reports `reset` or `retryExhausted` may or may not have run on the device, and its final is not waited for.

Each interim reply re-arms the request's deadline. An interim is dropped when its index is not greater than the last accepted one (so the
replies are in order over a non-in-order Reliable), or when its status is 0xF0 or more. Interims are not queued: each one is handed to its
handler when it is taken.

A peer restart (Reliable reports it through its owner hook) closes every open request with `reset`.

Reliable asks whether a request packet is still wanted before it sends the packet again (`DeliveryWanted`). A request is wanted while its
slot is open. A request that its requester has closed, by its deadline or by the connection's loss, is not sent again when the link comes back,
and the device's handler does not run for it. A packet that the stack has already queued for the link is sent when the link is up again, and it
is not asked about: under `Down::Wait` the stack keeps its queued packets through an outage, so such a request can still reach the device after
its requester has closed it. Under `Down::Fail` the queued packets are dropped, and the request's resend is then the one that is asked about.

## Responder

A request taken from the wire is checked and copied in `Take`, because the packet is valid only during the walk.

- A command with a reply takes a ticket. Its handler runs from `Process` in arrival order, with the ticket and the decoded body:
  `void(VUB::Ticket<Commands, Id>, const Body&)`, or `void(Ticket)` without a body.
- A command with `NoReply()` or `Fire()` takes no ticket, so a ticket being held does not refuse it. Its handler runs in `Take`, with the
  decoded body: `void(const Body&)`, or `void()` without a body.
- A bad body of a command with no reply is returned from `Take` as `invalidData`, so the stack counts it in its layer errors, and the handler
  does not run.

The ticket's operations:

- `Interim(status)` sends an interim reply. It is a compile error for a command that is not declared `Interim()`. It returns `ok`, `busy`
  while the last interim is unsent, `invalidParameter` after 255 interims, and `notAvailable` when the ticket is no longer live.
- `Finish(status)` or `Finish(status, body)` makes the final: `ok`, or `notAvailable` when the ticket is no longer live.
- `Interim` and `Finish` with a status of 0xF0 or more return `invalidParameter` and send nothing; the ticket stays open.
- `Open()` is true while the ticket is live and no final is made for its request.
- `Number()` is the request's number.

A ticket is live only when its slot still holds its generation and its request number, and the request is open. A ticket kept after its
slot has taken a later request answers `notAvailable` from every call, and `Open()` is false. The generation is one byte, so the slot's
generation repeats every 256 requests; the request number is what tells a later request in the same slot apart.

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
requester's deadline ends the request. In a Counted() layer that refusal is counted in `refused` (see Counters).

## Events

An event is a packet from one side with an id and a body, and no reply. The sender declares it with `Event<Id>()` and `Body<T>()` in the
command set; `Body<VUB::NoBody>()` is an event with no body. The receiver handles it with `OnEvent<Id, &Fn>`: `void(const Body&)`, or
`void()` for an event with no body. An event and a command may share a value (the kind byte tells them apart), and each set holds events
of its own: a side that handles an event has it in its set.

```cpp
namespace WikiEvents {

enum class Event : uint8 {
	fault = 0x40,
	ready = 0x41,
	alarm = 0x42
};


using DeviceEvents = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Event<Event::fault>()
			.Body<Motor::MotorFault>()
		.Event<Event::ready>()
			.Body<VUB::NoBody>()
		.Event<Event::alarm>()
			.Body<Motor::MotorFault>()
			.Delivered();
}>;


void OnFault(const Motor::MotorFault& fault);
void OnReady();


using PcHandlers = VUB::Handlers<
	VUB::OnEvent<Event::fault, &OnFault>,
	VUB::OnEvent<Event::ready, &OnReady>>;


// The device emits: an unacknowledged event, an event with no body, and a Delivered event.
template<typename StackT>
void EmitFromDevice(StackT& stack) {
	auto handle = stack.Requests();
	(void)handle.template Emit<Event::fault>(Motor::MotorFault{3});
	(void)handle.template Emit<Event::ready>();
	(void)handle.template Emit<Event::alarm>(Motor::MotorFault{5});
}

}
```

`Emit<Id>(body)` returns what the stack's send returns: `ok`, `busy` (the control queue or the window is full), `noInit` (Reliable has no
session) or `noConnection` (under `Down::Fail` while the link is down). Under `Down::Wait` the packet waits in the control queue until the
link returns. `Emit<Id>()` is the form for an event with no body.

- An event is unacknowledged by default: Reliable sends it with the no-acknowledgement flag, so it is not resent and keeps no slot. The sender
  does not learn that it was lost.
- `Delivered()` sends the event sequenced, through Reliable. Its packet is resent as a request is, and it is handled once. When Reliable gives
  up on it (`retryExhausted`) or the peer restarts with it pending (`reset`), the sender counts a loss in `eventsLost`. An acknowledged event
  counts nothing. A `Delivered` event needs Reliable below the layer: without it the build stops with
  `an event with Delivered() needs Reliable below Requests: a Delivered event is sequenced by Reliable`.
- The first sequenced send of a session is `busy`: Reliable starts the session with it and queues nothing. The sends after the session's
  sync is acknowledged are accepted.
- The handler of an event runs in the walk that takes its packet, after the requests received earlier in that walk, so the events and the
  requests of one burst run in arrival order.
- An event whose id is unknown to the receiver, or that the receiver has no handler for, is refused: it is not handled and it is counted in
  `refused` (see Counters). A body shorter than the handler's body is `invalidData` and is counted in the stack's layer errors. A longer body
  is read as far as the handler's body goes, so a newer sender may add fields at the end.

What is built: a side that declares no event has no event code. A side that emits only has the `Emit` code. A side with an `OnEvent`
handler has the receive code. A side with events declared but no `OnEvent` handler has no receive code either, so an EVENT packet that
arrives there is `invalidData` and is counted in the stack's layer errors, not in `refused`; the same holds for a side that declares no
event. A set with no `Delivered()` event has no outcome code for events, and a layer without Reliable below has none at all.

## Counters

`Counted()` on the layer's Step gives three counters, each a 16-bit count that stops at 65535:

- `refused`: an event with an unknown id or with no handler, and a request whose refusal the ring had no room for.
- `lateReplies`: a FINAL or an INTERIM whose number and tag match no open request (for example one that came after its deadline).
- `eventsLost`: a `Delivered` event whose outcome is not ok (see Events).

They are read with `Refused()`, `LateReplies()` and `EventsLost()` on the handle or on the layer. Without `Counted()` the layer keeps no
counter and builds no code that counts.

## Wire

The layer's packets are the payloads of the block below. The kind is byte 0. Numbers and tags are as above; all multi-byte fields are
little-endian.

| Kind | Layout | Packet |
|---|---|---|
| 0x01 REQUEST | `01 id(1) number(2) tag(1) body` | from the requester |
| 0x02 INTERIM | `02 number(2) tag(1) index(1) status(1)` | from the responder |
| 0x03 FINAL | `03 number(2) tag(1) code(1)`, and for code 0: `status(1) body` | from the responder |

Any other first byte is refused as `invalidData` by the layer's `Take`.

The FINAL codes are a fixed table of the layer, not the core's `ResultStatus` numbers:

| Code | Result | Sent for |
|---|---|---|
| 0 | ok (`status` and the body follow) | the handler's `Finish` |
| 1 | operationCancelled | not sent in this version |
| 2 | notSupported | an unknown command id, or no handler |
| 3 | invalidData | a body too short, or a bad bool |
| 4 | busy | no free ticket |
| 5 | timeout | the responder's ticket deadline |
| 6 | invalidParameter | reserved; not sent in this version |
| 7 to 255 | not used | a requester reads one as invalidData |

Statuses 0xF0 to 0xFF are reserved for the layer. A status of that range given to `Finish` or `Interim` returns `invalidParameter` and sends
nothing. A FINAL or an INTERIM that arrives with such a status is refused as `invalidData`. Ids 0xF0 to 0xFF are reserved too: a command or
event id of that range is a compile error.

The layer's tests check these bytes. For the start command of the test description, whose `MotorStart` has a ramp field after its direction
(default 300): a request with number 7 and `MotorStart{1200, 1}` is `01 12 07 00 07 B0 04 01 2C 01`; its interim `warming` is
`02 07 00 07 01 01`; its final with the report is `03 07 00 07 00 00 B0 04 52 03`. For the stop command with number 1 and tag 1, the request
is `01 13 01 00 01` and its final `started` is `03 01 00 01 00 00`. The bytes of the example above are the same layout with a body of
`speedRpm` and `direction` only.

## Failure paths

| Case | Requester sees | Responder does |
|---|---|---|
| The final is answered in the same round as the request | `ok` | one packet back, with the acknowledgement of the request |
| The device holds the request too long | `timeout` at the requester's deadline | its ticket deadline closes the ticket with code 5 (sent as a final); a later `Finish` is `notAvailable` |
| The request is lost and Reliable gives up | `retryExhausted` | the ticket may run; its final is dropped at the requester |
| The request is lost, the link goes down, and the deadline passes while it is down | `timeout`; the resend is not sent (`DeliveryWanted`) | the handler does not run |
| The peer restarts with a request open | `reset` for every open request of the peer | every ticket is closed; a final not yet sent is discarded |
| The link goes down | `noConnection` for `FailWhileDown` commands; the others wait for their deadline | nothing is sent by the layer while the link is down |
| The link has not come up yet | `noConnection` for `FailWhileDown` commands | none |
| The requester's table is full | `busy` at once; no number used | none |
| No free ticket | `busy` (code 4) | a `Fire` or `NoReply` still runs its handler |
| A late or mismatched FINAL or INTERIM | dropped | none |
| A ticket whose slot has taken a later request | the ticket answers `notAvailable`; the later request is untouched | none |
| A number that wraps while an earlier request with the same number is still open | the old request's final does not close the new one (tag) | none |

## Size

Measured with `arm-none-eabi-g++` 15.2 (`-Os`, `--gc-sections`, nano specs) by `.tool/size/size.sh`, row by row. Each row is the same chain
without the layer (the row's baseline) and the same chain with the layer; the difference is in bytes, text and RAM (data plus bss). The chains
are: CRC-16/MODBUS over COBS over a stream link; the same without the CRC transform; and `VUB::ReliableOf<40, 4>` over the CRC and COBS.

| Layer on the chain | Role | Send sites | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---|---:|---:|---:|---:|---:|
| CRC-16/MODBUS over COBS | responder: one command, `Tickets(1)`, `Refusals(2)` | 0 | 1404 | 40 | 1432 | 40 |
| CRC-16/MODBUS over COBS | requester: `Open(2)` | 1 | 1484 | 40 | 1536 | 40 |
| CRC-16/MODBUS over COBS | requester: `Open(2)` | 4 | 1928 | 40 | 1972 | 40 |
| CRC-16/MODBUS over COBS | both: `Open(2)`, `Tickets(1)`, `Refusals(2)` | 1 | 2404 | 76 | 2460 | 76 |
| COBS over a stream link | responder | 0 | 1380 | 120 | 1392 | 120 |
| COBS over a stream link | requester | 1 | 1484 | 120 | 1524 | 120 |
| COBS over a stream link | both | 1 | 2400 | 156 | 2432 | 156 |
| `ReliableOf<40, 4>` over CRC and COBS | responder | 0 | 1596 | 44 | 1612 | 44 |
| `ReliableOf<40, 4>` over CRC and COBS | requester | 1 | 1896 | 44 | 1940 | 44 |
| `ReliableOf<40, 4>` over CRC and COBS | both | 1 | 2792 | 84 | 2824 | 84 |

Events and counters, measured as differences against the same program with no event declared and no counter (the layer on
`VUB::ReliableOf<40, 4>` over CRC-16/MODBUS and COBS, arm-none-eabi-g++ 15.2, `-Os`, nano specs). The emit-only row against a
program with no send includes the application-side send entry of the layer above Reliable and the body encoder, which any
`Send` needs too. The row for a program that already sends measures the emit alone.

| Feature | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| one event declared, not used | 0 | 0 | 0 | 0 |
| emit only (plain event), the layer already sends a request | 108 | 0 | 104 | 0 |
| emit only (plain event), no send in the program | 1060 | 0 | 1144 | 0 |
| receive only (one OnEvent handler) | 384 | 4 | 420 | 4 |
| emit and receive (plain event) | 1396 | 4 | 1496 | 4 |
| `Delivered()` against plain, emit only | 0 | 0 | 0 | 0 |
| `Delivered()` against plain, emit and receive | 0 | 0 | 0 | 0 |
| `Counted()` alone | 16 | 8 | 28 | 8 |
| `Delivered()` against plain, emit and receive, `Counted()` | 36 | 0 | 44 | 0 |

## Not built

These are not in this layer:

- Cancel: no CANCEL packet (kind 0x05), no cancel handler call, no `Cancel` or `CancelAll` call, and no cancelRefused interim.
- Ping (0xF0) and health (0xF1), with their counters.
- `Await`.
