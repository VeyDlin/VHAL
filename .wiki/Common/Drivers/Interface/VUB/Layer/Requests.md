# VUB::Requests

The requests layer carries requests from a requester to a device and their replies. A request has a typed body and one final reply, and
it may have interim replies before the final. A command may have no reply at all. The layer also carries events, and the built-in ping and
health requests. A device serves commands with handlers. A requester sends them, and receives the finals, the interims and the events.

The layer sits above [VUB::Reliable](Reliable) for delivery, or directly above a transform, a framing or a link. Its packets are the
payloads of the block below. A body is one packet: the layer does not carry large data, addressing or subscriptions (see the
[status in the README](../README.md#status)).

## Example

The device serves two commands and sends its packets over a Reliable layer. The snippet is compiled by
`.tool/sim/TestVubRequestsLayerWiki.cpp`.

```cpp
#include <Drivers/Interface/VUB/Framing/Cobs.h>
#include <Drivers/Interface/VUB/Layer/Reliable.h>
#include <Drivers/Interface/VUB/Requests.h>
#include <Drivers/Interface/VUB/Stack.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
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
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;
```

The requester has the same command set, with no handlers of its own. It has two requester slots, which is the number of requests it can
have open at once. Its link is the same, and its calls are below. The snippet is compiled by the same test.

```cpp
void OnStartFinal(const VUB::Final<Motor::MotorStatus, Motor::MotorReport>& reply) {
	(void)reply;
}


void OnStartInterim(const VUB::Progress<Motor::MotorStatus>& progress) {
	(void)progress;
}


void OnStopFinal(const VUB::Final<Motor::MotorStatus, VUB::NoBody>& reply) {
	(void)reply;
}


using RequesterLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.Layer<VUB::Requests<Motor::MotorCommands, VUB::Handlers<>>>().Open(2)
		.Layer<VUB::ReliableOf<40, 4>>()
			.Retries(3)
			.TimeoutMs(200)
			.Window(4)
		.Transform<VUB::Crc<Checksum::Crc16Modbus>>()
		.Framing<VUB::Cobs>()
		.Link<UartLink>()
		.MaxPayload(40)
		.OnReceive<&HandlePacket>()
		.Clock<&MicrosecondClock>();
}>;


// A start request with an interim reply, and a stop request, which has no body and no interim replies.
inline void SendCommands(RequesterLink& link) {
	(void)link.Requests().Send<Motor::MotorCommand::start, &OnStartFinal, &OnStartInterim>(Motor::MotorStart{1200, 1});
	(void)link.Requests().Send<Motor::MotorCommand::stop, &OnStopFinal>();
}
```

The layer is added with `.Layer<VUB::Requests<Commands, Handlers>>()`. `Commands` is a `VUB::CommandSet` and `Handlers` a `VUB::Handlers` list
(see [Handlers](#handlers-and-the-device)). The layer needs a clock (`Clock<&Fn>()`), and the Reliable layer below it needs its session
(see [Session](#session)).

## Commands

A command set is built with `VUB::Commands()`. A command is named by a value of an `enum class` with an underlying type of `uint8`, and
the value is its id on the wire. Ids 0xF0 to 0xFF are reserved for the layer. A command set holds at most 240 commands and at most 240 events. For each command the builder declares its shape:

- `.Request<Body>()`: the body of the request. A command with no body has no `Request`.
- `.Reply<Status>()` or `.Reply<Status, Body>()`: the status and the body of the final reply. A command with a reply is sent with `Send`.
- `.Interim()`: the command may have interim replies. It follows `Reply`.
- `.Cancellable()`: the command takes a cancel. It follows `Reply` (see [Cancel](#cancel)).
- `.NoReply()`: the command has no reply. Its request has a delivery outcome, so it is sent with `Send` and needs Reliable below.
- `.Fire()`: the command has no reply and no delivery outcome. It is sent with `Fire`, with no slot and no handler.
- `.FailWhileDown()`: the request is closed with `noConnection` when the connection goes down, and a `Send` of it while the connection is
  down returns `noConnection` (see [Loss, restart and link down](#loss-restart-and-link-down)).
- `.ReplyTimeoutMs(ms)`: the requester's deadline for a command with a reply. A `Send` of such a command needs it.
- `.TicketTimeoutMs(ms)`: the device's deadline for a ticket. A served command with a reply needs it.

Both timeouts are from 1 to 2147483 milliseconds.

The layer's step options set its size and its features. Each builds only what it sizes:

| Option | What it gives | Default |
|---|---|---|
| `.Open(n)` | `n` requester slots, the requests open at once | 0: no requester slot |
| `.Tickets(n)` | `n` responder tickets, the requests the device holds for its handlers at once | 0; a served command with a reply needs one or more |
| `.Refusals(n)` | `n` entries in the refusal ring, the refusals waiting to be sent from `Process` | 4 |
| `.Counted()` | the counters `refused`, `lateReplies` and `eventsLost` (see [Counters](#counters)) | off |
| `.Ping(maxPadding)` | sends pings; needs `Open(n)` | off (see [Ping](#ping)) |
| `.AnswerPing(maxPadding)` | answers pings | off |
| `.Health<U>()` | sends health requests; needs `Open(n)` | off (see [Health](#health)) |
| `.AnswerHealth<U, &Fill>()` | answers health requests; needs `Counted()` | off |
| `.BuiltInTimeoutMs(ms)` | the deadline of a ping or a health request | 1000 ms |
| `.Pending(n)` | packets a concurrent layer holds for `Process` | 4 |
| `.Awaitable()` | the requests may be awaited on a concurrent layer | off |

The counts of `Open`, `Tickets`, `Refusals` and `Pending` are 8-bit: each is from 0 to 255.

A part that is off has no storage: its member is an empty type.

## Sending and final replies

`link.Requests()` gives the handle. `Send<Id, &OnFinal, &OnInterim>(body)` sends a request of a command with a reply. The interim handler is
optional: `Send<Id, &OnFinal>(body)` is a request with no interim handler. A command with no body is sent without an argument, as
`Send<Id, &OnFinal>()`, and the same holds for `Fire`, `Emit` and `Await`, which give the body `VUB::NoBody`.

- `Send` returns `Result<RequestId>`. The `RequestId` is the request's number, which is never 0.
- `Fire<Id>(body)` sends a command declared `Fire()`. It takes no slot and has no handler. Its errors are those of `Send`, except `noInit`.
- A `Send` of a command with no reply (`NoReply()`) takes its delivery outcome as its answer, through the final handler
  `void (*)(ResultStatus)`.

Errors come back at once, with nothing sent and no number used:

- `busy`: no free slot, or the layer below refuses the packet.
- `noConnection`: the command is `FailWhileDown` and the connection is down, or the link has not come up.
- `noInit`: Reliable has no session, or, with no Reliable below, `SetSession` has not been called. A `Fire` needs no session.

The final handler is `void (*)(const VUB::Final<Status, Reply>&)`. A `Final` has three fields. `result` is `ok` for a reply that arrived, and
otherwise the local or refusal result. `status` and `body` hold the reply. A command with no body has `VUB::NoBody` as its reply type.
The interim handler is `void (*)(const VUB::Progress<Status>&)`, with the fields `result` and `status`.

### Requester slots and numbers

A request takes a free slot and the next number. The number counter skips 0 and any number that an open request has, and it wraps after
65535. Each request also has a tag, one byte from its own counter, which advances with the number. The tag and the number identify a
request together, so a late packet of a request whose number is reused is dropped. A refused `Send` uses neither the slot nor a
number, so the number that follows is the next one.

Each interim reply re-arms the request's deadline. An interim is dropped when its index is not greater than the last one accepted, so the
replies are in order over a Reliable that does not keep order, and when its status is 0xF0 or more. Interims are not queued: each one is
handed to its handler when it is taken, and none is lost. Ten interims in one walk run ten handlers.

### Session

With no Reliable below, the requester needs a session value: `SetSession(value)` on the handle. Each boot should give a different value, for
example a stored counter or a random number. The first number is the low 16 bits of `value` times 0x9E37, and the first tag is the low
byte of `value`. Number 0 is never sent, so a `value` of 0 gives number 1 first, and a tag of 0 is valid. The first numbers of values that follow each other are far
apart, so a request of another boot that is still open on the responder is not taken for a request of this boot, and its final does not
close one. Until `SetSession` is called, `Send` returns `noInit` and nothing is sent. `Fire` needs no session.

With Reliable below, there is no `SetSession` on the handle, and the session is Reliable's (see [Reliable](Reliable)). The layer then takes
the owner of Reliable's outcomes and restarts, so a request learns the delivery of its packet, and a peer restart closes its requests. The
builder sets the layer's option `ownerBelow` to true for that chain. Without Reliable (a transform, a framing or a link directly below),
the layer has no outcomes and no restarts, and its code for them is not built. A request then ends with its final, its deadline, or the
loss of the connection for a `FailWhileDown` command.

## When the handlers run

- A final or an interim that arrives on the wire runs its handler inside the walk that takes the packet, so the handler can send at once.
  The slot of a final is freed before its handler runs, so the handler can send the next request into that slot.
- A ticket handler of a served command runs from the layer's `Process`, in arrival order. The final it makes goes out in the same `Process`.
- A command with no reply that arrives (its `Fire` or `NoReply` request) runs its handler inside the walk that takes it, after the requests
  taken earlier in that walk.
- A deadline, a delivery outcome, the loss of the connection, and a peer restart set the request's result. Its handler runs once, in the next
  `Process`, from the layer's own context and not from inside Reliable.
- Each request's final handler runs once.

## Interims

On the device, `Interim(status)` sends an interim reply of a command declared `Interim()`. It returns:

- `ok` when the interim is queued.
- `busy` while the last interim is unsent, and while a cancel-refused interim (0xF8, see [Cancel](#cancel)) waits to be sent.
- `invalidParameter` after 255 interims of one request, and for a status of 0xF0 or more.
- `notAvailable` when the ticket is not live (see [Tickets](#tickets-on-the-device)).

A set with no interim command has no interim code and no interim fields in its tickets, so `Interim` is a compile error for a command that is
not declared `Interim()`.

## Tickets on the device

A request taken from the wire is checked and copied in `Take`, because the packet is valid only during the walk.

- A served command with a reply takes a ticket. Its handler runs from `Process` in arrival order, with the ticket and the decoded body:
  `void(VUB::Ticket<Commands, Id>, const Body&)`, or `void(Ticket)` for a command with no body.
- A command with `NoReply()` or `Fire()` takes no ticket, so a ticket being held does not refuse it. Its handler runs in `Take`, with the
  decoded body: `void(const Body&)`, or `void()` for a command with no body. A bad body of such a command is returned from `Take` as
  `invalidData`, so the stack counts it in its layer errors, and the handler does not run.

The ticket's operations:

- `Interim(status)` sends an interim reply (see [Interims](#interims)).
- `Finish(status)` or `Finish(status, body)` makes the final. It returns `ok`, or `notAvailable` when the ticket is not live.
- `Interim` and `Finish` with a status of 0xF0 or more return `invalidParameter` and send nothing. The ticket stays open.
- `Open()` is true while the ticket is live and no final is made for its request.
- `Number()` is the request's number.
- `Cancelled()` makes the final `operationCancelled` while the ticket is open. It exists only in a set with a cancellable command.

A ticket is live only when its slot still holds its generation and its request number, and the request is open. A ticket kept after its slot
has taken another request answers `notAvailable` from every call, and `Open()` is false. The generation is one byte, so the slot's generation
repeats every 256 requests. The request number tells apart the requests that use the same slot.

A final made in a handler goes out in the same `Process`, after the handler's interims. A final made outside a handler goes at the next
`Process`. A final that Reliable refuses as busy is kept in the ticket and sent again. A ticket that is never finished ends at its
`TicketTimeoutMs` with a final of code 5 (`timeout`), and an interim re-arms that deadline.

A request whose number is open on the responder is handled by its tag. The same tag is a duplicate and is ignored. Another tag is refused
with `busy`. A peer restart closes every ticket, and the final of an open ticket is discarded.

### Refusals

A request the layer does not admit is answered by a final from the refusal ring. The ring is decided in `Take` and sent from `Process`. The
codes are the fixed codes of the wire (see [Wire](#wire)): 2 for an unknown command id or a command with no handler, 3 for a body that is
too short or has a bad bool, and 4 (busy) for no free ticket. A refused command with no reply gets no answer. A refusal that finds the ring
full is dropped, and the requester's deadline ends the request. On a `Counted()` layer that refusal is counted in `refused` (see
[Counters](#counters)).

### Handlers and the device

`VUB::Handlers<...>` lists the handlers of the served commands: `VUB::On<Id, &Fn>` for a command with a reply, `VUB::OnEvent<Id, &Fn>` for
an event (see [Events](#events)), and `VUB::OnCancel<Id, &Fn>` for the cancel handler of a cancellable command. A list that does not match its
command set is a compile error (see [Build-time checks](#build-time-checks)). A layer with no command with a reply needs no tickets.

## Cancel

A command takes a cancel when it is declared `Cancellable()` after its reply. `Cancel` on a command that is not declared so is a compile
error, and so is `CancelAll` in a set with no cancellable command. A set with no cancellable command has no cancel code, and a requester
with no slots has no cancel state.

Requester:

- `Cancel<Id>(id)` returns `ok` when the request is open. A second cancel of the same request returns `ok` and sends nothing. It returns
  `notFound` when no open request has the number, and it never returns `busy`.
- The requester sends a CANCEL packet (kind 0x05, the request's number and tag). The request stays open. Its end is the responder's final:
  the real result, or `operationCancelled`.
- With Reliable below, a CANCEL is held until Reliable reports the REQUEST delivered, and then it is sent. A final that arrives first closes
  the request and drops the held CANCEL. With no Reliable, the CANCEL is sent at once, after its REQUEST, since the path keeps its order.
- A CANCEL that the stack does not take at once is tried again by each `Process` until it is taken or its request ends. This holds on a
  concurrent layer too. A refusal that is not busy, such as `noConnection`, is retried in the same way.
- A request whose cancel gets no answer ends with `operationCancelled` at its deadline. A held CANCEL counts as a cancel. A cancel does not
  move the deadline.
- `CancelAll()` sends one CANCEL per open request of a cancellable command. A request sent after the call is not cancelled. The number 0 is not used on the wire.
- The outcome of a CANCEL packet is ignored. Reliable resends a CANCEL while its request is open, and does not resend it after the request
  has closed.

Responder:

- `OnCancel<Command::start, &OnStartCancel>` names the cancel handler, `void(VUB::Ticket<Commands, Id>)`. The handler runs from `Process`,
  never inside `Take`.
- When the handler returns with the ticket open, the layer sends `operationCancelled` for it. `ticket.Cancelled()` makes that final from the
  handler while the ticket is open.
- A CANCEL that arrives while its request is received, and before its handler has run (a REQUEST and its CANCEL in one burst), stops the
  handler. The layer then sends `operationCancelled`.
- A CANCEL of a command that is not cancellable gets an interim reply with status 0xF8 (cancel refused). The requester gives it to the
  interim handler of that command as a `Progress` with `result` `notSupported`, and the request runs to its own end. A command with no
  interim handler drops it.
- A CANCEL with a number that is not open, with another tag, or with the number 0 is dropped without a reply.
- The ticket's deadline and a peer restart run the cancel handler of a cancellable command once, and then close the ticket. At the deadline
  the final is `timeout` (code 5), unless the handler finished the ticket. At a restart the final is discarded.

## Events

An event is a packet from one side with an id and a body, and no reply. The sender declares it with `Event<Id>()` and `Body<T>()` in the
command set. `Body<VUB::NoBody>()` declares an event with no body. The receiver handles it with `OnEvent<Id, &Fn>`, which takes
`void(const Body&)`, or `void()` for an event with no body. An event and a command may share a value, since the kind byte tells them apart.
Each set holds the events of its own side: a side that handles an event has it in its set.

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


void OnFault(const Motor::MotorFault& fault) {
	(void)fault;
}


void OnReady() {
}


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

`Emit<Id>(body)` returns the stack's send result: `ok`, `busy` (the send queue or the window is full, or the link cannot take the packet),
`noInit` (Reliable has no session), or `noConnection` (under `Down::Fail` while the link is down). Under `Down::Wait` the packet waits in the
control queue until the link returns. `Emit<Id>()` is the form for an event with no body.

- An event is unacknowledged by default. Reliable sends it with the no-acknowledgement flag, so it is not resent and keeps no slot. The
  sender does not learn that it was lost.
- `Delivered()` sends the event sequenced, through Reliable. Its packet is resent as a request is, and it is handled once. When Reliable
  gives up on it (`retryExhausted`), or the peer restarts with it pending (`reset`), the sender counts a loss in `eventsLost`. An acknowledged
  event counts nothing. A `Delivered` event needs Reliable below the layer. Without it, the build stops with
  `an event with Delivered() needs Reliable below Requests: a Delivered event is sequenced by Reliable`.
- The first sequenced send of a session is `busy`: Reliable starts the session with it and queues nothing. The sends after the session is
  established are accepted. The sync's acknowledgement establishes the session, and with the in-order option that happens in the `Process`
  after the held packets of the peer's old session have gone up (see [Reliable](Reliable)).
- The handler of an event runs in the walk that takes its packet, after the requests received earlier in that walk, so the events and the
  requests of one burst run in arrival order.
- An event whose id is unknown to the receiver, or that has no handler there, is refused. It is not handled, and it is counted in `refused`.
  A body shorter than the handler's body is `invalidData` and is counted in the stack's layer errors. A longer body is read as far as the
  handler's body goes, so a newer sender may add fields at the end.

A side builds only what it uses. A side that declares no event has no event code. A side that emits has the `Emit` code, and a side with an
`OnEvent` handler has the receive code. A side with events declared but no `OnEvent` handler has no receive code, so an EVENT packet that
arrives there is `invalidData` and is counted in the stack's layer errors, not in `refused`. The same holds for a side that declares no
event. A set with no `Delivered()` event has no outcome code for events. A layer without Reliable below has none at all.

## Counters

`Counted()` on the layer's step gives three counters. Each is a 16-bit count that stops at 65535:

- `refused`: an event with an unknown id or with no handler, and a request whose refusal the ring had no room for.
- `lateReplies`: a FINAL or an INTERIM whose number and tag match no open request, for example one that came after its deadline.
- `eventsLost`: a `Delivered` event whose outcome is not ok (see [Events](#events)).

They are read with `Refused()`, `LateReplies()` and `EventsLost()` on the handle or on the layer. Without `Counted()` the layer keeps no
counter and builds no code that counts.

## Ping

The built-in ping measures the round trip to the peer. It is a request with the id 0xF0, which the layer answers itself, with no command and
no handler of its own. A side takes one or both of two steps:

- `.Ping(maxPadding)` sends pings. It needs `Open(n)` of at least 1, and it holds the slots of the requests. It builds no responder.
- `.AnswerPing(maxPadding)` answers the pings it receives. It needs no `Open`, no handler and no ticket. It builds one reply buffer of
  10 bytes plus `maxPadding`, and the check of the request in `Take` and the send in `Process`.

A layer with neither step has no ping code, no reply buffer and no slot field.

```cpp
// --- snippet: Layer/Requests.md, ping ---
// The requester: four slots, a ping of up to 16 bytes of padding, and the default deadline of 1000 ms.
using PingLink = VUB::Stack<+[]() {
	return VUB::Builder()
		.SendQueue(4)
		.Layer<VUB::Requests<PingCommands, VUB::Handlers<>>>().Open(4)
			.Ping(16)
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The handler of a ping: the result, and the round trip in microseconds (0 unless the result is ok).
void OnPingDone(ResultStatus result, uint32 roundTripUs) {
	(void)result;
	(void)roundTripUs;
}


// A ping with 8 bytes of padding. The request goes out in the next Process, and the handler runs when the reply has come.
inline ResultStatus SendPingExample(PingLink& link) {
	return link.Requests().Ping<&OnPingDone>(8);
}
// --- end snippet ---
```

The snippet uses the command set `PingCommands` of `TestVubRequestsPing.cpp`. The answering side is built with `.AnswerPing(maxPadding)` in the
same chain, with or without its own commands. A side that does both has both steps. `Ping<&Handler>(padding)` returns at once:

- `ok` when a slot is free. The slot is taken, and no number is used until the `Process` that sends the ping.
- `busy` when every slot is taken.
- `messageTooLong` when `padding` is above the layer's `maxPadding`.
- `noInit` without Reliable below, while no session is given (see [Session](#session)).

The request is sent by the next `Process`, which stamps it with that Process's clock. A send that the layer below refuses as busy is tried
again in the next `Process`, with that Process's stamp. The deadline runs from the first `Process` after the call, whether or not the
request has been sent by then. A request that could not be sent ends with the result of its deadline, and nothing of it is sent after that.
The padding is `index & 0xFF` for each byte, and the reply echoes it. The handler runs once, from `Process`, with the result and the round
trip:

- `ok` when the reply arrives. The round trip is the clock of that `Process` minus the stamp the reply echoes, so it is correct when the
  32-bit clock wraps during the round trip.
- `timeout` when the deadline passes first.
- `retryExhausted` when Reliable gives up first, and `reset` when the peer restarts with the ping open.
- `noConnection` when the link refuses the packet under `Down::Fail`.
- `notSupported` when the peer has no ping.
- `invalidData` when the reply has no stamp, or a status byte.

A ping is not a `FailWhileDown` command. While the link is down it waits, and its deadline ends it. A reply that comes after the deadline is
dropped. A ping that a handler sends from inside a final handler is sent by the next `Process`.

The deadline is `.BuiltInTimeoutMs(ms)` on the layer. It is 1000 ms by default, and it must be from 1 to 2147483. Without `.Ping(maxPadding)`
or `.Health<U>()`, `BuiltInTimeoutMs` is an error.

The answering side takes a ping in its `Take`. The reply goes into one reply buffer, which the layer's `Process` sends. A second ping while
that buffer is unsent is refused `busy` through the refusal ring, and is dropped when the layer has `Refusals(0)`. A ping whose padding is
above the side's `maxPadding` is refused `invalidData`. The buffer holds 10 bytes plus `maxPadding`.

A side without `.AnswerPing` does not answer a ping. Where it serves commands, the ping is refused as an unknown command (`notSupported`).
Where it serves nothing, the ping gets no reply, and the requester's deadline ends it.

The wire is the request `01 F0 number(2) tag(1) stamp(4) padding`, and the reply `03 number(2) tag(1) 00 00 stamp(4) padding`. A ping with
number 9, stamp 0x00012345 and no padding is `01 F0 09 00 09 45 23 01 00`, and its reply is `03 09 00 09 00 00 45 23 01 00`.

## Health

The built-in health request reads the peer's counters and its user body. It is a request with the id 0xF1, answered by the layer itself, with
no command and no handler. Like ping, it has two steps, and a side takes one or both:

- `.Health<U>()` asks. It needs `Open(n)` of at least 1. `U` is the user body of the reply, or `VUB::NoBody` when there is none. The handler
  of `Health<&Handler>()` takes a `VUB::HealthReport<U>`. The report is decoded in `Take` with the handler's own type, so the asking side
  needs no size of its own beyond the check that the handler's `U` is the one of the step.
- `.AnswerHealth<U, &Fill>()` answers. `Fill` is `void (*)(U&)`, and it writes the user body. It runs once per request, from `Process`, and
  the reply goes out in the same `Process`. It needs no `Open`, no handler and no ticket. It needs `Counted()` on the layer, because the
  reply carries the layer's refused, lateReplies and eventsLost counts. The stack needs `CountDrops()` and `HighWater()`, and under Reliable
  it needs `Counted()` on Reliable (see [Counters](#counters)).

The reply is 28 bytes of counters, then `U`. The counters are the bytes of `VUB::HealthCounters` as they lie in memory, which is the wire
order:

| Bytes (after the 6-byte FINAL header) | Field |
|---|---|
| 0 to 15 | the eight drop counters: ringFull, queueFull, tooLong, framingError, transformError, layerError, sendError, linkDown (2 each) |
| 16 to 17 | resends |
| 18 to 19 | peerRestarts |
| 20 | highWaterRing (1 byte, saturating) |
| 21 | highWaterControl (1 byte, saturating) |
| 22 to 23 | refused |
| 24 to 25 | lateReplies |
| 26 to 27 | eventsLost |
| 28 on | the user body `U` |

A reply with zero counters and a body of uptime 60 s, reset 1, state 2 is `03 01 00 01 00 00`, then 28 zero bytes, then `3C 00 00 00 01 02`.
The counters are copied as they lie, so the layer needs a little-endian host (a `static_assert` checks it). A report that ends locally (a
deadline, a link loss, a restart) has the result set and nothing else. A reply whose body is shorter than `U` has `invalidData`. A longer one
is read for the fields `U` has.

The health request uses the deadline of `.BuiltInTimeoutMs(ms)`, as ping does, and it takes the same slot, session and `busy` rules. The layer
has one reply buffer for its built-in requests, which holds the larger of the ping reply and the health reply. A second built-in request while
the buffer is unsent is refused `busy` through the refusal ring. A refusal that finds the ring full is counted in `refused` on a `Counted()`
layer, as a refused served command is, and is dropped otherwise. A queued health request follows the same deadline rule as a ping (see
[Ping](#ping)).

```cpp
// --- snippet: Layer/Requests.md, health ---
// The asking side: two slots, and a health request whose reply carries a HealthBody. The handler gets the report.
using HealthAsker = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, PcHandlers>>().Open(2).Tickets(1)
			.Health<HealthCheck::HealthBody>()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;


// The handler of the report: the result, the responder's counters, and its user body.
void OnHealthReport(const VUB::HealthReport<HealthCheck::HealthBody>& report) {
	(void)report;
}


// A health request. The report goes to OnHealthReport in the Process that takes the reply, or with a local result.
inline ResultStatus AskHealth(HealthAsker& asker) {
	return asker.Requests().Health<&OnHealthReport>();
}
```

```cpp
// --- snippet: Layer/Requests.md, health answer ---
// The answering side: Counted() keeps the counts of the reply, Reliable below is Counted(), and the stack is HighWater().
using HealthAnswerer = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.SendQueue(4)
		.Layer<VUB::Requests<HealthCommands, DeviceHandlers>>().Counted()
			.AnswerHealth<HealthCheck::HealthBody, &FillHealth>()
		.Layer<VUB::ReliableOf<40, 4>>().Counted().Retries(3).TimeoutMs(200).Window(4)
		.Transform<XorTransform>()
		.Link<ReportingPacketEnd>()
		.HighWater()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&TestReceive>()
		.Clock<&TestClock>();
}>;
```

The snippets use the command set `HealthCommands`, the fill `FillHealth` and the body `HealthCheck::HealthBody` of `TestVubRequestsHealth.cpp`.

## Tasks and Await

A stack built with `Concurrent()` lets tasks other than the one that calls `Process` use the requests layer. The layer takes the stack's
flag through its options. A layer that is not concurrent has no code or storage for any of this (see the size rows at the end).

The requests layer takes `Pending(n)` (the packets the layer holds for `Process`, 4 by default) and `Awaitable()` (the requests may be
awaited). The builder takes `Concurrent()`. The requester of `TestVubRequestsConcurrent.cpp` has this form, with the default queue:

```cpp
// --- snippet: Layer/Requests.md, the concurrent requester ---
using Requester = VUB::Stack<+[]() {
	return VUB::Builder()
		.CountDrops()
		.Layer<VUB::Requests<JobCommands, RequesterHandlers>>().Open(4).Awaitable().Counted()
		.Layer<VUB::ReliableOf<40, 4>>().Retries(3).TimeoutMs(200).Window(4)
		.Link<ReportingPacketEnd>()
		.MaxPayload(40)
		.ReceiveQueue(4)
		.OnReceive<&ConcurrentReceive>()
		.Clock<&ConcurrentClock>()
		.Concurrent();
}>;
// --- end snippet ---
```

### Calls from another task

On a concurrent layer these calls may come from any task while `Process` runs in another: `Send`, `Fire`, `Ping`, `Cancel`, `CancelAll`,
`Emit`, `Await`, `SetSession`, the counters `Refused()`, `LateReplies()` and `EventsLost()`, and the ticket calls `Interim`, `Finish`,
`Cancelled` and `Open`.

- Each call touches the layer's tables only inside a `System::CriticalSection` section. The sections are short: a slot is read or changed, a
  packet is copied into the queue, a waiter is written. No user handler runs in a section.
- A user body is encoded on the caller's stack, outside the section. A final's body is decoded by the final handler outside the section.
  The body of an awaited final is decoded inside the section, since the waiter must still be alive while it is written. No user code runs
  there.
- `Process` takes what it needs from a table inside a section, and acts on it outside. Each handler (final, interim, event, command with no
  reply, ticket handler, cancel handler) runs outside every section, from `Process` or from the receive walk.

### The queue of a concurrent requester

A request, a fire, a cancel, a ping or an event of a concurrent layer is copied into a queue of `Pending(n)` packets, each as large as the
largest packet of the layer. `Process` sends the queue from its own outbox, oldest first, outside every section. It removes a packet only
after the outbox has taken it.

- `Send`, `Fire`, `Emit` and `Ping` return `ok` when the packet is queued, and `busy` when the queue or the request table is full. A busy
  result takes no number and no slot, and the caller sends again.
- The order rule: the packets of one task go out in the order of its calls, since the queue is first in, first out, and each request gets
  its number when it is queued. The order rule does not cover ping and health requests. They are sent by `Process`, so a ping or a health
  request made before a `Send` of the same task can go out after that `Send`.
- The handlers of one receive burst run in ticket order, not in call order. The layer orders a `Fire` and an event against the requests of
  the same walk, not the handlers of the requests among themselves.
- A refusal of the outbox other than busy, for example `noConnection` under `Fail`, settles the request the packet names with that result.
  Its final handler gets it in `Process`. An awaited request gets it in its waiter.
- A packet whose request has closed while it waited is removed without being sent. That is a request closed by its deadline, by a cancel, or
  by the restart of the peer. A REQUEST with a slot goes out only while its number and tag are open (a ping's too), and a CANCEL only while
  its request is open. A fire and an event have no slot and are always sent. The packets of a request that never reached the wire are not
  sent after its deadline, however long the stack refused them.
- `Process` sends the queue at its end, after the deadlines, the handlers of the finals, the interims and the events, and the responder's
  work of this `Process`. So a request, a fire, an event or a cancel made in a handler goes out in the `Process` that ran the handler, behind
  the packets queued before it.
- A packet's length is kept in 16 bits, so a packet of up to the layer's largest packet size (more than 255 bytes is allowed) is sent whole.

### Await

`Await<Id, Interim>(body)` sends the request, waits until its final is in or its limit passes, and returns the final. It is a member of the
handle, built only when a caller uses it. `Await<Id>()` is the form for a command with no body: it gives the body `VUB::NoBody`. It needs `Awaitable()` and `Concurrent()` (a `static_assert` names each). It is never called from
the task that runs `Process`, nor from a handler: the wait would wait for the `Process` that has to run it, until its limit.

```cpp
// --- snippet: Layer/Requests.md, the await ---
// Await of Command::run, called from the calling thread. The final is returned to the caller; no final handler runs for it.
VUB::Final<Job::JobStatus, Job::JobReport> AwaitRun(RequesterHandle handle, uint16 amount) {
	Job::JobRequest body{};
	body.amount = amount;
	return handle.Await<Job::Command::run, &OnRunInterim>(body);
}
// --- end snippet ---
```

- The waiter is an object on the caller's stack. The request's slot points at it, and `Process` writes the final into it, inside a section,
  with its typed decode. No final handler runs for an awaited request.
- A refused send is returned in `result`, and no wait starts.
- The wait is `WaitCondition::WaitFor<WaitCondition::RtosYield>`: one poll per millisecond through `System::rtosDelayMsHandle`, the RTOS delay
  of the port. Each poll checks the final inside a section. With no handle set, the polls repeat without a pause, so the task spends its time
  in the polls until the final or the limit.
- A command with no reply returns its `ResultStatus`, the outcome of its delivery. Its request has no deadline of its own, so the wait ends
  with that outcome. While the link is down under `Down::Wait`, the request waits for the link, and the wait waits with it. A command with
  `FailWhileDown()` ends its wait with `noConnection` as soon as the link goes down, since its request does not wait for the link.
- The limit is the command's reply deadline plus a grace of 250 ms, in system ticks. On a running system the request's own deadline, which the
  stack's clock gives, ends the wait first.
- A wait that gives up clears the slot's pointer inside a section, then returns `timeout`, unless the final was written meanwhile. In that
  case the final is returned. A final that arrives after the give-up is handled by the layer as for any request. If the request is still open
  it closes the request with no handler. If the request has closed, it counts in `lateReplies`.

The orders of the final and the give-up are decided by the section alone. `System::CriticalSection` is the only lock of the layer. Every read
and write of a waiter's `done` flag, and of a slot's pointer to it, is inside a section. A waiter leaves its frame only after `done` is set or
its pointer is cleared, both inside a section. On a dual-core target the section must exclude the other core as well, as the port's section
does (interrupts masked and a nesting depth). The layer relies on that and on no other lock. The four orders, and the test that forces each:

| Order | Result | Test |
|---|---|---|
| the final is written while the wait runs, or the wait starts after the final is in | the wait returns the final | `TestAwaitAnsweredWithInterim` |
| the deadline of the request passes first (the stack's clock) | `timeout` from `Process`; a final that arrives after it counts in `lateReplies` | `TestAwaitAbandonedThenLateAnswerFreesSlotOnce` |
| the wait gives up while the request is open; the final arrives after the give-up | `timeout`; the final closes the request with no handler, and is not late | `TestAwaitAbandonedFinalBeforeDeadline` |
| the final lands between the wait's last check and its abandon | the wait returns the final (`Abandon` sees `done`) | `TestAwaitFinalBetweenTheCheckAndTheAbandon` |

The test forces the window through its own `System::CriticalSection` and its own `rtosDelayMsHandle`, without any hook in the product. The
handle runs in the waiting task's poll, after its last check. It moves the tick past the limit. The next check of the final, and then the
abandon's section, are the waiting task's next two section entries. The test's section hook lets the other task take the final before the
abandon's lock is taken, so the abandon finds the final written, and the `Await` returns it.

## Loss, restart and link down

A request's end is its first result: its final, its deadline, a Reliable outcome of `retryExhausted` or `reset`, or `noConnection`. A
`FailWhileDown` command is closed with `noConnection` when the connection goes down. A `Send` of it while the connection is down, or while
the link has not come up, returns `noConnection` at once. The other commands wait for the connection, and their deadlines run. A request
that Reliable reports as `reset` or `retryExhausted` may or may not have run on the device, and its final is not waited for.

A peer restart, reported by Reliable through its owner hook, closes every open request with `reset`.

Reliable asks whether a request packet is still wanted before it sends the packet again (`DeliveryWanted`). A request is wanted while its
slot is open, and a CANCEL is wanted while its request is open. A request that its requester has closed, by its deadline or by the
connection's loss, is not sent when the link comes back, and the device's handler does not run for it. This holds for a request sent while
the link is down, and for a request whose copy the stack had queued when the link went down. When the link comes back, the stack drops the
copies that Reliable sends again, with the part of a frame that was written. Reliable sends each packet that is still wanted, in the order of
the packets' numbers. The send at the return is not counted as a try of the packet. Under `Down::Fail`, the queued packets are dropped when the
link goes down, and the packet that is sent again is the one that is asked about.

| Case | Requester sees | Responder does |
|---|---|---|
| The final is answered in the same round as the request | `ok` | one packet back, with the acknowledgement of the request |
| The device holds the request too long | `timeout` at the requester's deadline | its ticket deadline closes the ticket with code 5 (sent as a final); a later `Finish` is `notAvailable` |
| The request is lost and Reliable gives up | `retryExhausted` | the ticket may run; its final is dropped at the requester |
| The request is lost, the link goes down, and the deadline passes while it is down | `timeout`; the resend is not sent (`DeliveryWanted`) | the handler does not run |
| The request is sent while the link is down, and the deadline passes while it is down | `timeout`; its copy is dropped when the link returns, and the resend is not sent (`DeliveryWanted`) | the handler does not run |
| The request is queued when the link goes down, and the deadline passes while it is down | `timeout`; its copy is dropped when the link returns, and the resend is not sent (`DeliveryWanted`) | the handler does not run |
| The peer restarts with a request open | `reset` for every open request of the peer | every ticket is closed; a final that has not been sent is discarded |
| The link goes down | `noConnection` for `FailWhileDown` commands; the others wait for their deadline | nothing is sent by the layer while the link is down |
| The link has not come up | `noConnection` for `FailWhileDown` commands | none |
| The requester's table is full | `busy` at once; no number used | none |
| No free ticket | `busy` (code 4) | a `Fire` or `NoReply` still runs its handler |
| A late or mismatched FINAL or INTERIM | dropped | none |
| A FINAL of code 0 with no status byte | dropped and counted in the stack's `layerError`; the request stays open | none |
| A ticket whose slot has taken a later request | the ticket answers `notAvailable`; the later request is untouched | none |
| A number that wraps while a request with the same number is still open | the old request's final does not close the new one (tag) | none |
| A cancel while the REQUEST is not acknowledged | the CANCEL is held; the request ends with the real result or `operationCancelled` | the CANCEL is taken after the REQUEST is delivered |
| A cancel that gets no answer, or a cancel lost on the line with no Reliable | `operationCancelled` at the requester's deadline | the cancel handler ran, or never saw the cancel |
| A cancel racing the final | the first of the two packets wins | a CANCEL that arrives after the final is dropped |
| A cancel of a command that is not cancellable | a progress with `notSupported`; the request runs to its own end | the interim with status 0xF8 |
| The ticket's deadline of a cancellable command | `timeout` (code 5) | the cancel handler runs once, then the final is sent |
| A peer restart with a cancellable ticket open | `reset` | the cancel handler runs once; the ticket's final is discarded |

## Wire

The layer's packets are the payloads of the block below. The kind is byte 0. Numbers and tags are as above. All multi-byte fields are
little-endian.

| Kind | Layout | Packet |
|---|---|---|
| 0x01 REQUEST | `01 id(1) number(2) tag(1) body` | from the requester |
| 0x02 INTERIM | `02 number(2) tag(1) index(1) status(1)` | from the responder |
| 0x03 FINAL | `03 number(2) tag(1) code(1)`, and for code 0: `status(1) body` | from the responder |
| 0x04 EVENT | `04 id(1) body` | from the side that emits; taken by a side with an `OnEvent` handler |
| 0x05 CANCEL | `05 number(2) tag(1)` | from the requester; only in a set with a cancellable command |

Any other first byte is refused as `invalidData` by the layer's `Take`. A CANCEL is refused the same way in a set with no cancellable command.
An INTERIM with status 0xF8 is the cancel-refused reply to a CANCEL of a command that is not cancellable. Statuses 0xF0 to 0xFF are reserved
for the layer, as above.

The FINAL codes are a fixed table of the layer. They are not the core's `ResultStatus` numbers:

| Code | Result | Sent for |
|---|---|---|
| 0 | ok (`status` and the body follow) | the handler's `Finish` |
| 1 | operationCancelled | a cancel: the cancel handler left the ticket open, or the REQUEST and its CANCEL came in one burst |
| 2 | notSupported | an unknown command id, or no handler |
| 3 | invalidData | a body too short, or a bad bool |
| 4 | busy | no free ticket |
| 5 | timeout | the responder's ticket deadline |
| 6 | invalidParameter | reserved; not sent in this version |
| 7 to 255 | not used | a requester reads one as invalidData |

Statuses 0xF0 to 0xFF are reserved for the layer. A status of that range given to `Finish` or `Interim` returns `invalidParameter` and sends
nothing. A FINAL or an INTERIM that arrives with such a status is refused as `invalidData`. Ids 0xF0 to 0xFF are reserved too: a command or
event id of that range is a compile error.

The layer's tests check these bytes. In the test suite's command set, `MotorStart` has a ramp field after its direction, with the default
300. A request with number 7 and `MotorStart{1200, 1}` is `01 12 07 00 07 B0 04 01 2C 01`. Its interim `warming` is `02 07 00 07 01 01`,
and its final with the report is `03 07 00 07 00 00 B0 04 52 03`. For the stop command with number 1 and tag 1, the request is
`01 13 01 00 01`, and its final `started` is `03 01 00 01 00 00`. The bytes of the example above have the same layout, with a body of
`speedRpm` and `direction` only.

## Build-time checks

Each rule is a `static_assert` or a failed check with the message shown.

| Rule | Message |
|---|---|
| A `Send` on a layer with no `Open(n)` | `Send needs requester slots: give the layer Open(n) with n of at least 1` |
| A `Send` of a command with no reply (`NoReply()`) and no Reliable below | `a command with a delivery outcome needs Reliable below Requests; use .Fire() for no delivery at all` |
| A `Fire` of a command with a reply | `Fire takes a command with no reply: a command with a reply is sent with Send` |
| A `Send` of a command with a reply and no `ReplyTimeoutMs` | `a command with a reply needs ReplyTimeoutMs: the requester's deadline` |
| A served command with a reply, and `Tickets(0)` (checked when the layer takes a packet) | `the layer serves a command and has no tickets: give Tickets(n) with n of at least 1` |
| `Ticket::Interim` on a command without `.Interim()` | `Ticket::Interim needs the command to be declared Interim(): add .Interim() after its Reply` |
| `ReplyTimeoutMs` of 0 | `ReplyTimeoutMs must be above 0` |
| `ReplyTimeoutMs` above 2147483 | `ReplyTimeoutMs must be at most 2147483 (the deadlines are signed 32-bit microsecond counts)` |
| `TicketTimeoutMs` of 0 | `TicketTimeoutMs must be above 0` |
| `TicketTimeoutMs` above 2147483 | `TicketTimeoutMs must be at most 2147483 (the deadlines are signed 32-bit microsecond counts)` |
| A `Ping` on a layer without the step `.Ping(maxPadding)` | `Ping needs the layer's Step .Ping(maxPadding)` |
| A `Ping` on a layer with no `Open(n)` | `Ping needs requester slots: give the layer Open(n) with n of at least 1` |
| `BuiltInTimeoutMs` on a layer with neither `.Ping(maxPadding)` nor `.Health<U>()` | `BuiltInTimeoutMs needs the Step .Ping(maxPadding) or .Health<U>() of the layer` |
| `Health<&Handler>()` on a layer without the step `.Health<U>()` | `Health needs the layer's Step .Health<U>()` |
| `Await` on a layer with no `Open(n)` | `Await needs requester slots: give the layer Open(n) with n of at least 1` |
| `Await` of a command with no reply, and no Reliable below | `Await of a command with no reply needs Reliable below Requests` |
| `Await` of a `Fire()` command | `a Fire command has no reply: it cannot be awaited` |
| `BuiltInTimeoutMs` of 0 or above 2147483 | `BuiltInTimeoutMs must be from 1 to 2147483 milliseconds` |
| A `Ping` handler with another signature | `a ping handler has the signature void (ResultStatus, uint32)` |
| A command id of 0xF0 or more | `a command id is 0xF0 or more: the ids 0xF0 to 0xFF are reserved for the layer` (an event id has the matching message) |
| A `Delivered` event with no Reliable below | `an event with Delivered() needs Reliable below Requests: a Delivered event is sequenced by Reliable` |
| A handler whose signature does not match its command (`CheckHandlers`, `CheckFinal`, `CheckProgress`) | the messages of the handler list checks, in `Requests/Handlers.h` |
| A `Cancel` of a command that is not `Cancellable()`, or a `CancelAll` in a set with no cancellable command | a compile error at the call |
| A `Ticket::Cancelled` in a set with no cancellable command | a compile error at the call |

The rules of the layer's stack, which the stack checks when it is built, have these messages:

| Rule | Message |
|---|---|
| `Health<U>()` on a layer with no `Open(n)` | `Health needs requester slots: give the layer Open(n) with n of at least 1` |
| `AnswerHealth` on a layer without `Counted()` | `AnswerHealth needs this layer's Counted(): add .Counted() to the requests layer` |
| `AnswerHealth` on a stack without `CountDrops()` | `a layer of the chain sends the drop counts (Requests with AnswerHealth does): add CountDrops() to the stack` |
| `AnswerHealth` on a stack without `HighWater()` | `a layer of the chain sends the high-water marks (Requests with AnswerHealth does): add HighWater() to the stack` |
| `AnswerHealth` under Reliable that is not `Counted()` | `a layer of the chain sends Reliable's counters (Requests with AnswerHealth does): add Counted() to the Reliable layer` |
| a health handler with another signature | `a health handler has the signature void (const VUB::HealthReport<U>&)` |
| a handler whose `U` has another size than the step's | `the health handler's user body is not the body of the layer's Step .Health<U>()` |
| `Await` on a layer without `Awaitable()` | `Await needs the requests layer's Step .Awaitable(): add .Awaitable() to the requests layer` |
| `Await` on a stack without `Concurrent()` | `Await needs a concurrent stack: add .Concurrent() to the builder` |
| `Pending(0)` | `Pending needs at least 1 packet: give the layer Pending(n) with n of at least 1` |
| The packet queue of a concurrent layer, with a count outside 1 to 255 | `Pending needs from 1 to 255 packets` |

## Size

Each size is measured with `arm-none-eabi-g++.exe (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203` (`-Os`, `--gc-sections`, nano specs). Text is flash, and RAM is data plus bss.

The layer rows are measured by `.tool/size/size.sh`, row by row. Each row is the same chain without the layer (the row's baseline) and the same
chain with the layer. The difference is in bytes. The chains are CRC-16/MODBUS over COBS over a stream link, the same without the CRC
transform, and `VUB::ReliableOf<40, 4>` over the CRC and COBS.

| Layer on the chain | Role | Send sites | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---|---:|---:|---:|---:|---:|
| CRC-16/MODBUS over COBS | responder: one command, `Tickets(1)`, `Refusals(2)` | 0 | 1368 | 84 | 1384 | 84 |
| CRC-16/MODBUS over COBS | requester: `Open(2)` | 1 | 1512 | 84 | 1536 | 84 |
| CRC-16/MODBUS over COBS | requester: `Open(2)` | 4 | 1940 | 84 | 1968 | 84 |
| CRC-16/MODBUS over COBS | both: `Open(2)`, `Tickets(1)`, `Refusals(2)` | 1 | 2424 | 120 | 2464 | 120 |
| COBS over a stream link | responder | 0 | 1356 | 80 | 1344 | 80 |
| COBS over a stream link | requester | 1 | 1460 | 80 | 1484 | 80 |
| COBS over a stream link | both | 1 | 2364 | 116 | 2384 | 116 |
| `ReliableOf<40, 4>` over CRC and COBS | responder | 0 | 1584 | 44 | 1572 | 44 |
| `ReliableOf<40, 4>` over CRC and COBS | requester | 1 | 1960 | 44 | 1960 | 44 |
| `ReliableOf<40, 4>` over CRC and COBS | both | 1 | 2856 | 84 | 2848 | 84 |

With no cancellable command in the set, the three chains keep the values of the table above in each role. The RAM of each role is the same on
Cortex-M4 and Cortex-M0+. The text is larger on Cortex-M0+ in the CRC-16/MODBUS roles and in the COBS requester and both roles. In the COBS responder and the
`ReliableOf` responder and both roles the text is smaller on Cortex-M0+ (1356 against 1344, 1584 against 1572, 2856 against 2848), and in the
`ReliableOf` requester it is the same on both (1960).

### Cancel

The cancel cost is the same program with the command made cancellable, minus the program with it not cancellable. The cancellable program has
a cancel handler on the responder, and calls `Cancel` and `CancelAll` in the requester (roles 2 and 3). The chain is CRC-16/MODBUS over COBS
over a stream link.

| Cancel on the layer | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| responder: `Tickets(1)`, `Refusals(2)`, a cancel handler | +452 | 0 | +464 | 0 |
| requester: `Open(2)`, `Cancel` and `CancelAll` called | +372 | +8 | +388 | +8 |
| both | +716 | +8 | +720 | +8 |

The slots, as `sizeof` on Cortex-M4 and M0+ (the same on both), with a 16-byte body for the ticket:

| Slot | Neither | Cancel | Interim | Both |
|---|---:|---:|---:|---:|
| requester slot | 16 | 20 | 24 | 24 |
| responder ticket | 32 | 36 | 36 | 40 |

A set with a cancellable command and no interim command has no interim field in its slots. The 0xF8 refusal is then dropped by the requester,
and the responder keeps a flag in the ticket. In the size configuration the ticket's body is 6 bytes, and the cancel flags fit in its
padding, so the responder row has no RAM cost there.

### Events and counters

Measured as differences against the same program with no event declared and no counter. The layer is on `VUB::ReliableOf<40, 4>` over
CRC-16/MODBUS and COBS, with `arm-none-eabi-g++.exe (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203`, `-Os`, and nano specs. The emit-only row against a program with no send includes the
application-side send entry of the layer above Reliable and the body encoder, which any `Send` needs too. The row for a program that already
sends measures the emit alone.

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

### Ping

The ping is measured as the difference of the same role with and without the step, over `ReliableOf<40, 4>` over CRC and COBS (bytes, text
and RAM).

| Side | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| sends: `.Ping(16)`, `Open(2)` | +448 | 0 | +456 | 0 |
| answers, next to served commands: `.AnswerPing(16)`, `Tickets(1)`, `Refusals(2)` | +160 | +32 | +176 | +32 |
| answers, nothing served, `.AnswerPing(16)` | +1412 | +64 | +1428 | +64 |
| answers, nothing served, `.AnswerPing(16)`, `Refusals(0)` | +1212 | +44 | +1268 | +44 |
| both: `.Ping(16)`, `.AnswerPing(16)`, `Open(2)`, `Tickets(1)`, `Refusals(2)` | +536 | +36 | +528 | +36 |

A side that answers and serves nothing pays for the send path its reply needs: the stack's send thunks and the reliable layer's send, which a
side that only receives does not otherwise contain. Next to served commands, that path is already there, and the ping adds the row above.
The rows without a ping step are unchanged from the table in [Size](#size).

### Health

The health rows are the marginal cost of the step next to the ping: `.tool/size/ConfigHealth.cpp` over `ReliableOf<40, 4>` on CRC-16/MODBUS
over COBS over a stream link. The program has a served command, the ping on both sides, and the counters of the stack (`Counted()` on the
layer and on Reliable, `CountDrops()` and `HighWater()`) in both programs of each row. The base is that program, and the row adds the step
named.

| Health, next to ping and the served command | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| asks: `.Health<HealthBody>()` | +276 | +0 | +272 | +0 |
| answers: `.AnswerHealth<HealthBody, &Fill>()` | +604 | +12 | +528 | +12 |
| both | +788 | +12 | +704 | +12 |

A program with no requester has no send path for a built-in request. Its first built-in request pays for that path: the slots of `Open(2)`,
the queue and send of a built-in request, and the encoding of the request. Measured on a served command with the counters, asking for health
is +1156 bytes of text and +40 bytes of RAM on M4 (+1224 and +40 on M0+). The rows above already include that path in a program that sends
pings.

### Concurrent

The concurrent rows are in `.tool/size/size.sh`, measured with `arm-none-eabi-g++.exe (Arm GNU Toolchain 15.2.Rel1 (Build arm-15.86)) 15.2.1 20251203` (`-Os`, nano specs) by `ConfigConcurrent.cpp`. The
program is CRC-16/MODBUS over COBS over the stream link of `SizeWire.h`, with the port's section (the nesting helper with PRIMASK) and the
tick and RTOS handle of the port. The base of each row is the same program with no layer and no `Concurrent()`.

| Program (difference to no layer) | M4 text | M4 RAM | M0+ text | M0+ RAM |
|---|---:|---:|---:|---:|
| requester, `Open(2)`, plain | 1644 | 84 | 1644 | 84 |
| requester, `Open(2)`, `Pending(4)`, `Awaitable()`, `Concurrent()` | 2164 | 148 | 2248 | 148 |
| responder, `Tickets(1)`, `Refusals(2)`, plain | 1444 | 84 | 1440 | 84 |
| responder, the same, `Pending(4)`, `Awaitable()`, `Concurrent()` | 1664 | 92 | 1664 | 92 |
| `Await` of one request: the requester with the call, minus the requester without it | 460 | 4 | 460 | 4 |

The concurrent rows have `Concurrent()` but no staging ring: the requests go through the layer's handle, and the program does not call `Send`
from another task. A stack that also takes `Send` from another task adds `StageQueue(n)` and its ring (see
[Concurrent](../README.md#concurrent-the-concurrent-option-and-the-concurrent-flag)).

## Sources and tests

- `Layer/Requests.h`: the options, the step methods, the handle, and `Take`, `Process` and the delivery hooks. It composes the parts below.
- `Requests.h`: the umbrella header of the layer's public types.
- `Requests/Commands.h`: `CommandSet`, and the command and event builders (`Commands()`, `Reply`, `Interim`, `Fire`, `NoReply`, `Event`).
- `Requests/Handlers.h`: `Handlers`, `On`, `OnEvent`, `OnCancel`, `Final`, `Progress`, and the checks of a handler list.
- `Requests/Body.h`: the body codec and the `BodyOf` layouts.
- `Requests/Requester.h` (`RequesterOf`): the requester part. It holds the slots, the numbers and tags, the session, the link state, and the
  calls `Start`, `Fire`, the takes of a final and an interim, the deadlines, the outcomes and the restart.
- `Requests/Responder.h` (`ResponderOf`): the responder part. It holds the tickets and the refusal ring, and the calls of the received requests,
  the finals, the interims, the deadlines and the restart.
- `Requests/Events.h` (`EventsOf`): the emitter of the events and the receiver of the event packets. It holds no state.
- `Requests/BuiltIn.h`: the built-in ids, the ping's and the health's request and reply encoding, the health report, and the responder's reply
  buffer.
- `Requests/Concurrent.h`: the queue of a concurrent requester and the waiter of `Await`.
- `Requests/Ticket.h`: `RequestId`, `TicketOps` and `Ticket`. `Requests/Wire.h`: the wire kinds, the codes and their results, and the deadline
  compare.

A part that is off has no storage: its member is an empty type.

Tests: `TestVubRequestsWire.cpp` (the bytes), `TestVubRequestsRequester.cpp`, `TestVubRequestsResponder.cpp`, `TestVubRequestsPath.cpp`
(chains, links and restarts), `TestVubRequestsEvents.cpp` (events and counters), `TestVubRequestsPing.cpp`, `TestVubRequestsHealth.cpp`,
`TestVubRequestsCancel.cpp`, `TestVubRequestsConcurrent.cpp`, and `TestVubRequestsLayerWiki.cpp` (the examples above). Their command sets and
helpers are in `.tool/sim/RequestsChains.h` and `.tool/sim/RequestsMotor.h`.
