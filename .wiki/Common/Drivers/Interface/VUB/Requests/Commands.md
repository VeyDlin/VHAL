# VUB::Commands: the command description

A command description names every command and event of a product, with the body of each, the reply, and the options. The description
is written once, in a header that the device and the PC both include, so the two sides agree on the ids and the types.
`VUB::Commands()` starts the description, and `VUB::CommandSet<+[] { ... }>` is the set it gives: a type with compile-time tables.

Headers: `Common/Drivers/Interface/VUB/Requests/Commands.h`, and the handler lists in `Requests/Handlers.h` (see Handlers below). The bodies
are described in [Body](Body).

## The description

The bodies are structs with a `BodyOf` (see [Body](Body)), and a reply status is an enum class with a `uint8` underlying type:
`enum class MotorStatus : uint8 { started = 0, warming = 1, failed = 2 };`. The description takes them as types.

The ids of the commands and of the events are values of enum classes with a `uint8` underlying type. The value is the id on the wire.

```cpp
// The motor description, shared by the device and the PC.
namespace Motor {

enum class MotorCommand : uint8 {
	start = 0x12,
	stop = 0x13,
	reset = 0x14
};

enum class MotorEvent : uint8 {
	fault = 0x40
};

using MotorCommands = VUB::CommandSet<+[]() {
	return VUB::Commands()
		.Command<MotorCommand::start>()
			.Request<MotorStart>()
			.Reply<MotorStatus, MotorReport>()
			.Interim()
			.Cancellable()
			.ReplyTimeoutMs(5000)
			.TicketTimeoutMs(60000)
		.Command<MotorCommand::stop>()
			.Reply<MotorStatus>()
			.ReplyTimeoutMs(2000)
			.TicketTimeoutMs(2000)
		.Command<MotorCommand::reset>()
			.NoReply()
		.Event<MotorEvent::fault>()
			.Body<MotorFault>();
}>;

}
```

The chain is a captureless lambda, as for the command line's command set. The id is a template argument, so the id is checked at
compile time, and the type of the enum is part of the id: a value of another enum is not a command of the set. Ids are one byte and are
on the wire: they never change. A command with no `.Request<T>()` has the body `VUB::NoBody`, so `MotorCommand::stop` and
`MotorCommand::reset` take no body.

## The calls

| Call | Meaning |
|---|---|
| `Command<Id>()` | starts a command; `Id` is a value of an enum class with a `uint8` underlying type, below 0xF0 |
| `Request<T>()` | the request body; without it, `VUB::NoBody` |
| `Reply<Status>()` | the final carries a status only (level 3) |
| `Reply<Status, Body>()` | the final carries a status and a body (level 3) |
| `NoReply()` | no reply: the answer is the delivery outcome (level 2) |
| `Fire()` | no reply and no delivery outcome (level 1) |
| `Interim()` | the command may send interim replies (level 4) |
| `Cancellable()` | the command can be cancelled (level 4) |
| `ReplyTimeoutMs(n)` | the requester's deadline; `n` is above 0 |
| `TicketTimeoutMs(n)` | the responder's bound on a ticket; `n` is above 0 |
| `FailWhileDown()` | a request fails at once while the link is down, instead of waiting |
| `Event<Id>()` | starts an event; `Id` is a value of an enum class with a `uint8` underlying type, below 0xF0 |
| `Body<T>()` | the event's body; an event needs one (`VUB::NoBody` for an event with no body) |
| `Delivered()` | the event is sent with delivery (sequenced); without it, it is not acknowledged |

A `Reply` and a `NoReply` or `Fire` are one declaration: a command has exactly one. `Interim()` and `Cancellable()` follow a `Reply`
with a status. The options (`ReplyTimeoutMs`, `TicketTimeoutMs`, `FailWhileDown`) can follow in any order.

## Levels

| Level | Declaration | The requester gets | The responder's handler |
|---|---|---|---|
| 1 | `Fire()` | nothing: no outcome, no slot | takes the body only |
| 2 | `NoReply()` | the delivery outcome (`ResultStatus`) | takes the body only |
| 3 | `Reply<Status, Body>()` | one final: status and body | a ticket and the body |
| 4 | level 3 with `Interim()` or `Cancellable()` | interims and one final | a ticket and the body, and a cancel handler when cancellable |

## Lookups

The set has compile-time tables, in declaration order. They hold no names: a command or an event is found by its id.

| Name | Kind | Meaning |
|---|---|---|
| `Set::commandTable`, `Set::eventTable` | `std::array` of `CommandInfo`, `EventInfo` | the id, level, options, and the body views of each command or event |
| `Set::CommandById(id)`, `Set::EventById(id)` | function, `nullptr` when absent | a lookup by the byte id: the entry of the command or event with this id |
| `Set::commandCount`, `Set::eventCount` | `std::size_t` | the number of each |
| `Set::largestRequest`, `Set::largestReply`, `Set::largestEvent` | `std::size_t` | the largest body of each kind, in bytes (`maxSize`) |
| `VUB::CommandOf<Set, Id>` | type | `BodyType`, `StatusType`, `ReplyType` and `id` of the command with the value `Id` |
| `VUB::EventOf<Set, Id>` | type | `BodyType` and `id` of the event with the value `Id` |
| `VUB::CommandInfoOf<Set, Id>()` | constant | the entry of the command, with its level, flags and timeouts |
| `VUB::EventInfoOf<Set, Id>()` | constant | the entry of the event, with its delivery flag |

`CommandOf` and `EventOf` name a type, and `CommandInfoOf` and `EventInfoOf` are constant-expression lookups: an id the set does not have
is a compile error, and the lookups are used in constant expressions.

```cpp
static_assert(VUB::CommandOf<Motor::MotorCommands, Motor::MotorCommand::start>::id == 0x12);
static_assert(VUB::CommandInfoOf<Motor::MotorCommands, Motor::MotorCommand::start>().level == 4);
static_assert(Motor::MotorCommands::largestRequest == 5);

// The lookup of a received id: CommandById returns the entry of the command with this id, and the layer reads its body with the view.
const VUB::CommandInfo* LookUpStart() {
	return Motor::MotorCommands::CommandById(0x12);
}
```

## Limits

- At most 240 commands and at most 240 events. Command and event ids are one byte, and 0xF0 to 0xFF are reserved for the layer (ping
  and health).
- Ids are unique among the commands, and among the events. A command and an event may have the same value: the kind byte of a packet
  tells them apart.
- `ReplyTimeoutMs` and `TicketTimeoutMs` are at most 2147483 ms. The requests layer keeps its deadlines in microseconds and compares them as
  a signed 32-bit difference, so a longer timeout would wrap. `ReplyTimeoutMs must be at most 2147483 (the deadlines are signed 32-bit
  microsecond counts)` and the matching `TicketTimeoutMs` message are the compile errors.

## Errors

Each error is a `static_assert` or a failed check with its message. The text of the message is the one in the table.

| Error | Message (part) |
|---|---|
| a command argument that is not an enum class with a `uint8` underlying type | `a command id is a value of an enum class with a uint8 underlying type` |
| an event argument that is not an enum class with a `uint8` underlying type | `an event id is a value of an enum class with a uint8 underlying type` |
| a command id of 0xF0 or more | `the ids 0xF0 to 0xFF are reserved for the layer` |
| an event id of 0xF0 or more | `an event id is 0xF0 or more` |
| two commands with one value | `two commands have the same id value` |
| two events with one value | `two events have the same id value` |
| an id that no command of the set has | `no command of the set has this id` |
| an id that no event of the set has | `no event of the set has this id` |
| an event id where a command is expected | `an event has this id where a command is expected` |
| a command id where an event is expected | `a command has this id where an event is expected` |
| more than 240 commands | `more than 240 commands` |
| `Interim()` with no `Reply<Status>()` earlier in the chain | `Interim() needs a Reply<Status>() earlier in the chain` |
| `Cancellable()` with no reply, or after `NoReply()` | `Cancellable() is for a command with a Reply<Status>() earlier in the chain` |
| `ReplyTimeoutMs(0)` | `ReplyTimeoutMs must be above 0` |
| `TicketTimeoutMs(0)` | `TicketTimeoutMs must be above 0` |
| a status that is not an enum with a `uint8` underlying type | `a reply status is an enum class with a uint8 underlying type` |
| a second reply declaration (`Reply`, `NoReply`, `Fire`) | `a command has one reply declaration: Reply, NoReply or Fire` |
| a command with no reply declaration | `a command needs Reply<Status>(), NoReply() or Fire() after its Request()` |
| an event with no `Body<T>()` | `an event has no Body<T>()` |
| a body type with no `BodyOf` | `a request body type has no BodyOf` |

A status value of 0xF0 or more is not a compile error: the compiler does not list an enum's values, so only the underlying type is
checked here. The layer must refuse such a value when it sends it.

## Handlers

`Requests/Handlers.h` holds the handler lists. A list is a type, and each entry names a command or an event by its id and gives its
function:

```cpp
namespace Motor {

using DeviceHandlers = VUB::Handlers<
	VUB::On<MotorCommand::start, &OnStart>,
	VUB::OnCancel<MotorCommand::start, &OnStartCancel>,
	VUB::On<MotorCommand::stop, &OnStop>,
	VUB::On<MotorCommand::reset, &OnReset>>;

using PcHandlers = VUB::Handlers<
	VUB::OnEvent<MotorEvent::fault, &OnFault>>;

}
```

| Entry | Function type | For |
|---|---|---|
| `On<Id, Fn>` | `void(Ticket, const Body&)`, or `void(Ticket)` when the request has no body | a command with a reply (level 3, 4) |
| `On<Id, Fn>` | `void(const Body&)`, or `void()` with no request body | a `NoReply` or `Fire` command |
| `OnCancel<Id, Fn>` | `void(Ticket)` | a cancellable command |
| `OnEvent<Id, Fn>` | `void(const Body&)`, or `void()` with no body | an event |

`VUB::CheckHandlers<Set, Handlers>()` is a constant expression that checks a list against a set: each id is a command or an event
of the set; each function has the type of its entry, exactly; an `On` of a command with a reply needs a `TicketTimeoutMs`; an `OnCancel`
needs a `Cancellable()` command; and no two entries of one kind name the same command or event. A failed check is a `static_assert`
with its message. The `Ticket` class is the layer's, and the checks name it by `VUB::Ticket<Set, Id>`.

The requester's functions are checked by the same kind of call:

| Call | Function type |
|---|---|
| `CheckFinal<Set, Id, Fn>()` for a reply | `void(const VUB::Final<Status, Reply>&)` (for a reply with no body, `VUB::Final<Status, VUB::NoBody>`) |
| `CheckFinal<Set, Id, Fn>()` for `NoReply()` | `void(ResultStatus)` |
| `CheckProgress<Set, Id, Fn>()` | `void(const VUB::Progress<Status>&)`, for a command with `Interim()` or `Cancellable()` |

A `Fire` command has no final, so `CheckFinal` refuses it. `VUB::Final<StatusT, BodyT>` has `result`, `status` and `body`, and
`VUB::Progress<StatusT>` has `result` and `status`.

`VUB::HasHandler<HandlerList, Kind, Id>()` and `VUB::HandlerEntryOf<HandlerList, Kind, Id>::Type::fn` look a handler up by its kind
(`VUB::HandlerKind::on`, `onCancel` or `onEvent`) and its id.

## Not in this page

The checks that need the layer's stack belong to the layer, and it makes them when it is built: a `NoReply` command or a `Delivered`
event on a link without `Reliable` below it, the `Health` of a responder without the counters `Counted()` and `HighWater()`, and a
`Cancel` of a command that is not `Cancellable()`.
