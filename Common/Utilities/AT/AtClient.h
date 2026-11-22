#pragma once
#include "AtArgument.h"
#include "AtBinding.h"
#include "AtCommandSet.h"
#include "AtDialect.h"
#include "AtError.h"
#include "AtForms.h"
#include "AtValue.h"
#include "Modules/AtEcho.h"
#include <Utilities/CommandLine/CommandNameTree.h>
#include <Utilities/CommandLine/CommandRing.h>
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <VHAL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>


// The client: it sends the requests of its description, and reads the lines that the module answers with. The typed calls (Read, Write,
// Run, Test, Value) only queue a request, so they never write or wait. Feed() takes a byte into a ring, and may run in an interrupt.
// Process(nowMs) writes the request at the head of the queue, reads the bytes into lines, runs the handlers of the replies and of the
// URCs, and times the request out. The caller passes the time, so the client reads no clock.
//
// A request is one line on the wire, written at its head. Its reply is the info line of a read form (at most one), then a final line.
// A timed-out request is drained: the client sends AT and drops the lines. The first final line after the timeout only moves the drain
// on, and the next OK ends it, or the drain's own timeout does. Then the next request is sent. A late final line before the drain ends
// cannot answer the next request. A late OK after the drain ends can, since an OK cannot be told from the next request's OK.
//
// The line buffer keeps the first LineChars characters of a line, and a longer line is never a value: it cannot match a URC or a final,
// and an info line that is too long gives invalidData. The sizes come from the description, so no valid line is too long.


// The queue of requests: its depth is the template argument. The request on the wire is at the head, and it is counted.
template<std::size_t Depth>
struct AtQueue {
	static_assert(Depth > 0, "an AT client queue needs a depth of at least 1");

	static constexpr std::size_t depth = Depth;
};


// True for a type that the client's module list may hold. A module the list does not hold adds no code.
template<typename Module>
inline constexpr bool AtIsClientModule = false;


template<std::size_t Depth>
inline constexpr bool AtIsClientModule<AtQueue<Depth>> = true;


template<>
inline constexpr bool AtIsClientModule<AtEcho> = true;


// True for a queue type, whatever its depth.
template<typename Module>
inline constexpr bool AtIsQueueType = false;


template<std::size_t Depth>
inline constexpr bool AtIsQueueType<AtQueue<Depth>> = true;


// The number of times Module is in the list.
template<typename Module, typename... Modules>
constexpr std::size_t AtCountModule() {
	return ((std::is_same_v<Module, Modules> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
}


// The number of queue types in the list.
template<typename... Modules>
constexpr std::size_t AtQueueCount() {
	return ((AtIsQueueType<Modules> ? std::size_t{1} : std::size_t{0}) + ... + std::size_t{0});
}


// The depth of the queue in the list, or 2 when the list has none.
template<typename... Modules>
struct AtQueueDepthOf {
	static constexpr std::size_t value = 2;
};


template<std::size_t Depth, typename... Rest>
struct AtQueueDepthOf<AtQueue<Depth>, Rest...> {
	static constexpr std::size_t value = Depth;
};


template<typename Head, typename... Rest>
struct AtQueueDepthOf<Head, Rest...> : AtQueueDepthOf<Rest...> {
};


// A text that a request is written into. It is a fixed array, so the encoding never allocates. A write past the end sets overflow.
template<std::size_t Capacity>
class AtTextBuffer {
public:
	std::array<char, Capacity> characters{};
	std::size_t length = 0;
	bool overflow = false;


	std::size_t Write(std::string_view text) {
		for (char character : text) {
			if (length < Capacity) {
				characters[length] = character;
				length++;
			} else {
				overflow = true;
			}
		}

		return text.size();
	}
};


// The characters of an argument list: each field's value and one separator. A bound, since a field may be empty.
template<typename Tuple>
struct AtFieldChars;


template<typename... Arguments>
struct AtFieldChars<std::tuple<Arguments...>> {
	static constexpr std::size_t value = ((AtValueChars<typename Arguments::Type> + 1) + ... + std::size_t{0});
};


// The longest request text of the set, over every form of every command: "AT", the name, the mark of a form and the fields.
template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxRequestChars(std::index_sequence<C...>) {
	return std::max({std::size_t{1}, (4 + Set::names[C].size() + AtFieldChars<typename Set::template CommandType<C>::FieldArguments>::value)...});
}


// The bytes of the largest read value of the set. A command without a read form takes none.
template<typename Set, std::size_t C>
constexpr std::size_t AtReplyBytesOf() {
	if constexpr (Set::commands[C].hasForm[AtFormIndex(AtFormKind::read)]) {
		return sizeof(typename Set::template CommandType<C>::ReadType);
	} else {
		return 0;
	}
}


template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxReplyBytes(std::index_sequence<C...>) {
	return std::max({std::size_t{1}, AtReplyBytesOf<Set, C>()...});
}


// The characters of an info line: the prefix, one blank and the value. A command without a read form has none.
template<typename Set, std::size_t C>
constexpr std::size_t AtInfoChars() {
	if constexpr (Set::commands[C].hasForm[AtFormIndex(AtFormKind::read)]) {
		constexpr std::size_t prefix = Set::commands[C].options.replyPrefix.empty() ? Set::names[C].size() + 1 : Set::commands[C].options.replyPrefix.size();
		return prefix + 1 + AtValueChars<typename Set::template CommandType<C>::ReadType>;
	} else {
		return 0;
	}
}


// The characters of a URC line: its prefix and each argument with its comma.
template<typename Set, std::size_t U>
constexpr std::size_t AtUrcChars() {
	return Set::urcNames[U].size() + AtFieldChars<typename Set::template UrcType<U>::ArgumentTypes>::value;
}


// The characters of a final line: ERROR, the longest declared final or failure, and +CME ERROR: with a code or with the longest text.
template<typename Set>
constexpr std::size_t AtFinalChars() {
	std::size_t longest = 5;

	for (const AtCommandInfo& command : Set::commands) {
		for (std::size_t index = 0; index < command.options.finalCount; index++) {
			longest = std::max(longest, command.options.finals[index].size());
		}

		for (std::size_t index = 0; index < command.options.failureCount; index++) {
			longest = std::max(longest, command.options.failures[index].size());
		}
	}

	std::size_t cmeText = 5;

	for (const AtCmeTextEntry& entry : Set::cmeTexts) {
		cmeText = std::max(cmeText, entry.text.size());
	}

	return std::max(longest, std::size_t{12} + cmeText);
}


// The longest line the client reads: a request (for an echo), an info line, a URC or a final.
template<typename Set, std::size_t... C, std::size_t... U>
constexpr std::size_t AtMaxLineCharsOf(std::index_sequence<C...>, std::index_sequence<U...>) {
	return std::max({AtMaxRequestChars<Set>(std::index_sequence<C...>{}), AtInfoChars<Set, C>()..., AtUrcChars<Set, U>()..., AtFinalChars<Set>()});
}


// Writes one field: an integer or a bool as text, text quoted, and an optional field only when it has a value.
template<typename Sink, typename T>
requires (std::is_integral_v<T>)
std::size_t AtWriteField(Sink& out, T value) {
	return AtWriteValue(out, value);
}


template<typename Sink>
std::size_t AtWriteField(Sink& out, std::string_view text) {
	return AtWriteQuoted(out, text);
}


template<typename Sink, typename T>
std::size_t AtWriteField(Sink& out, const std::optional<T>& value) {
	if (value.has_value()) {
		return AtWriteField(out, *value);
	}

	return 0;
}


// The handler types of an argument list, as a tuple: a string is a view, and an optional argument is std::optional.
template<typename Arguments>
struct AtHandlerTupleOf;


template<typename... Arguments>
struct AtHandlerTupleOf<std::tuple<Arguments...>> {
	using Type = std::tuple<typename Arguments::HandlerType...>;
};


// Each argument's value as its handler type.
template<typename Arguments, typename Packed, std::size_t... K>
typename AtHandlerTupleOf<Arguments>::Type AtConvertFields(const Packed& packed, std::index_sequence<K...>) {
	return typename AtHandlerTupleOf<Arguments>::Type(typename std::tuple_element_t<K, Arguments>::HandlerType(std::get<K>(packed))...);
}


// A string longer than its capacity does not fit. Other values always fit.
template<typename Argument>
bool AtFieldFits(const typename Argument::HandlerType& value) {
	using T = typename Argument::Type;

	if constexpr (AtIsStringType<T>) {
		if constexpr (Argument::isOptional) {
			return !value.has_value() || value->size() <= AtCapacityOf<T>;
		} else {
			return value.size() <= AtCapacityOf<T>;
		}
	} else {
		static_cast<void>(value);
		return true;
	}
}


template<typename Arguments, typename Converted, std::size_t... K>
bool AtFieldsFit(const Converted& converted, std::index_sequence<K...>) {
	return (AtFieldFits<std::tuple_element_t<K, Arguments>>(std::get<K>(converted)) && ...);
}


// True when a field has a value to write. A required field always has one.
template<typename Argument>
bool AtFieldPresent(const typename Argument::HandlerType& value) {
	if constexpr (Argument::isOptional) {
		return value.has_value();
	} else {
		static_cast<void>(value);
		return true;
	}
}


// The number of fields to write: up to the last one with a value. Absent optional fields at the end are left out of the text.
template<typename Arguments, typename Converted, std::size_t... K>
std::size_t AtWrittenFields(const Converted& converted, std::index_sequence<K...>) {
	std::size_t count = 0;

	((AtFieldPresent<std::tuple_element_t<K, Arguments>>(std::get<K>(converted)) ? (count = K + 1) : count), ...);
	return count;
}


// Writes field K, and a comma before it when it is not the first. Fields from count on are not written.
template<std::size_t K, typename Sink, typename Value>
void AtWriteSeparated(Sink& out, const Value& value, std::size_t count) {
	if (K >= count) {
		return;
	}

	if constexpr (K > 0) {
		out.Write(std::string_view(","));
	}

	AtWriteField(out, value);
}


template<typename Converted, typename Sink, std::size_t... K>
void AtWriteFields(Sink& out, const Converted& converted, std::size_t count, std::index_sequence<K...>) {
	static_cast<void>(out);
	static_cast<void>(converted);
	static_cast<void>(count);
	(AtWriteSeparated<K>(out, std::get<K>(converted), count), ...);
}


// The values of an argument list in a URC, one per argument, as the arguments' value types. Presence is kept beside them.
template<typename Arguments>
struct AtStoredOf;


template<typename... Arguments>
struct AtStoredOf<std::tuple<Arguments...>> {
	using Type = std::tuple<typename Arguments::Type...>;
};


// Checks one URC binding: its prefix is declared, and its handler takes the arguments that the description declares for the prefix.
template<typename Set, typename Item>
struct AtUrcBindingCheck {
	static constexpr std::size_t index = Set::template UrcIndex<Item::nameText>();
	using Arguments = typename Set::template UrcType<index>::ArgumentTypes;
	static constexpr bool ok = std::is_same_v<typename Item::HandlerType, typename AtUrcHandler<Arguments>::Type>;

	static_assert(ok, "a URC handler does not match the arguments that the description declares for it");
};


// True when no two URC bindings name the same prefix.
template<typename... Items>
constexpr bool AtUrcNamesUnique() {
	std::array<std::string_view, sizeof...(Items)> names{Items::name...};

	for (std::size_t index = 0; index < names.size(); index++) {
		for (std::size_t other = 0; other < index; other++) {
			if (names[index] == names[other]) {
				return false;
			}
		}
	}

	return true;
}


template<typename Set, typename Bindings>
struct AtClientBindingCheck;


template<typename Set, typename... Bindings>
struct AtClientBindingCheck<Set, AtClientBinding<Bindings...>> {
	static constexpr bool ok = (AtUrcBindingCheck<Set, Bindings>::ok && ...) && AtUrcNamesUnique<Bindings...>();

	static_assert(AtUrcNamesUnique<Bindings...>(), "a URC prefix has two handlers in the client binding");
};


template<typename Set, typename Binding, typename... Modules>
class AtClient {
	static_assert((AtIsClientModule<Modules> && ...), "a client module is not an AtQueue or AtEcho");
	static_assert(AtCountModule<AtEcho, Modules...>() <= 1, "AtEcho is listed twice in the client modules");
	static_assert(AtQueueCount<Modules...>() <= 1, "AtQueue is listed twice in the client modules");
	static_assert(AtClientBindingCheck<Set, Binding>::ok, "a URC handler in the client binding does not match its description");

	static constexpr std::size_t QueueDepth = AtQueueDepthOf<Modules...>::value;

	static_assert(QueueDepth > 0, "an AT client queue needs a depth of at least 1");

	static constexpr bool HasEcho = AtCountModule<AtEcho, Modules...>() > 0;
	static constexpr std::size_t RingSize = 64;
	static constexpr std::size_t RequestChars = AtMaxRequestChars<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t ReplyBytes = AtMaxReplyBytes<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t LineChars = AtMaxLineCharsOf<Set>(std::make_index_sequence<Set::CommandCount>{}, std::make_index_sequence<Set::UrcCount>{});
	static constexpr uint32 DrainTimeoutMs = Set::DefaultTimeoutMs;

	// A completion runs the handler of a request with its outcome. The reply is the value of a read form, when one was stored.
	using Completion = void (*)(const std::byte* reply, AtStatus status);


	enum class RequestState : uint8 {
		queued,
		open,
		draining
	};


	// One request: its text, the command, its handler, and what the reply has given so far.
	struct Request {
		std::array<char, RequestChars> text{};
		std::size_t length = 0;
		std::size_t command = 0;
		Completion complete = nullptr;
		bool expectsValue = false;
		RequestState state = RequestState::queued;
		uint32 startMs = 0;
		uint32 drainStartMs = 0;
		bool drainFinalSeen = false;
		AtStatus status{};
		bool replyStored = false;
		alignas(8) std::byte reply[ReplyBytes]{};
	};


	CommandRing<RingSize> ring{};
	Print& out;
	uint32 droppedBytes = 0;
	std::array<char, LineChars> line{};
	std::size_t lineLength = 0;
	char lastTerminator = 0;
	std::array<Request, QueueDepth> queue{};
	std::size_t queueHead = 0;
	std::size_t queueCount = 0;
	bool echoPending = false;


public:
	AtClient(Print& output) : out(output) {
	}


	// Takes a byte from the receive interrupt. Returns false, and the byte is dropped and counted, when the ring is full.
	bool Feed(uint8 byte) {
		return ring.Push(byte);
	}


	// Writes the request at the head of the queue when none is on the wire, reads the bytes, and then times the request out when its time
	// is up. The bytes come first, so a reply that is already in the ring at the deadline is read, and the timeout is checked once all
	// of them are read.
	void Process(uint32 nowMs) {
		SendQueued(nowMs);

		uint8 byte = 0;

		while (ring.Pop(byte)) {
			ConsumeByte(byte, nowMs);
		}

		Expire(nowMs);
	}


	// The bytes the ring has dropped since the client started.
	uint32 DroppedBytes() {
		droppedBytes += ring.TakeLost();
		return droppedBytes;
	}


	// AT+X?. The handler gets the value of the read form, or the status of the request.
	template<AtText Name, auto Handler>
	AtStatus Read() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(std::is_same_v<decltype(Handler), void (*)(Result<typename Command::ReadType, AtStatus>)>,
			"a Read handler is void(Result<T, AtStatus>), with T the type of the read form");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		return Enqueue(text, index, &CompleteRead<Name, Handler>, true);
	}


	// AT+X=a,b. The arguments are the handler types of the write form, and a string is any text of its capacity or less.
	template<AtText Name, auto Handler, typename... Values>
	AtStatus Write(Values... values) {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;
		using Arguments = typename Command::WriteArguments;

		static_assert(Command::template CountOf<AtFormKind::write>() == 1, "AT command has no write form");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Write handler is void(AtStatus)");
		static_assert(AtArgumentsAccept<Arguments, Values...>(), "Write is given values that do not match the arguments of its write form");

		return Fields<index, Arguments, true>(std::make_tuple(values...), std::index_sequence_for<Values...>{}, &CompleteStatus<Handler>);
	}


	// AT+X
	template<AtText Name, auto Handler>
	AtStatus Run() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Run handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		return Enqueue(text, index, &CompleteStatus<Handler>, false);
	}


	// AT+X=?. The module's answer is only its final line, since a test form has no declared info line.
	template<AtText Name, auto Handler>
	AtStatus Test() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::test>() == 1, "AT command has no test form");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Test handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("=?"));
		return Enqueue(text, index, &CompleteStatus<Handler>, false);
	}


	// ATE0: the value of a basic command follows its letter.
	template<AtText Name, auto Handler, typename... Values>
	AtStatus Value(Values... values) {
		constexpr std::size_t treeIndex = Set::template TreeIndex<Name>();

		if constexpr (treeIndex >= Set::CommandCount) {
			return BuiltinValue<treeIndex, Handler>(values...);
		} else {
			using Command = typename Set::template CommandOf<Name>;
			using Arguments = typename Command::ValueArguments;

			static_assert(Command::isBasic, "a Value form is for a basic command, one letter");
			static_assert(Command::template CountOf<AtFormKind::value>() == 1, "AT command has no value form");
			static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Value handler is void(AtStatus)");
			static_assert(AtArgumentsAccept<Arguments, Values...>(), "Value is given values that do not match the arguments of its value form");

			return Fields<treeIndex, Arguments, false>(std::make_tuple(values...), std::index_sequence_for<Values...>{}, &CompleteStatus<Handler>);
		}
	}


private:
	// Calls visitor with an integral constant for each index below Count, the one that equals index.
	template<std::size_t Count, typename Visitor>
	static void VisitIndexed(std::size_t index, Visitor&& visitor) {
		static_cast<void>(index);
		static_cast<void>(visitor);

		[&]<std::size_t... I>(std::index_sequence<I...>) {
			((index == I ? (visitor(std::integral_constant<std::size_t, I>{}), 0) : 0), ...);
		}(std::make_index_sequence<Count>{});
	}


	static std::span<const CommandTreeNode> TreeSpan() {
		return std::span<const CommandTreeNode>(Set::tree.data(), Set::tree.size());
	}


	template<typename T>
	static const T& ValueIn(const std::byte* place) {
		return *std::launder(reinterpret_cast<const T*>(place));
	}


	// The start of every request: AT and the name of the command.
	template<std::size_t Capacity>
	static void WriteName(AtTextBuffer<Capacity>& text, std::size_t index) {
		text.Write(std::string_view("AT"));
		text.Write(Set::names[index]);
	}


	// ATE0, ATV1 or ATQ0: a builtin of the dialect with one uint8 value. Its request has the tree index as its command, so its timeout is the
	// set default and it has no info line.
	template<std::size_t TreeIndex, auto Handler, typename... Values>
	AtStatus BuiltinValue(Values... values) {
		constexpr AtBuiltin kind = Set::builtinList.kinds[TreeIndex - Set::CommandCount];

		static_assert(
			kind == AtBuiltin::echo || kind == AtBuiltin::verbose || kind == AtBuiltin::quiet,
			"Value is for the echo, verbose or quiet builtin, such as E"
		);
		static_assert(sizeof...(Values) == 1 && (std::is_same_v<Values, uint8> && ...), "a builtin takes one uint8 value, such as E0");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Value handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		text.Write(std::string_view("AT"));
		text.Write(Set::treeNames[TreeIndex]);
		(AtWriteValue(text, values), ...);
		return Enqueue(text, TreeIndex, &CompleteStatus<Handler>, false);
	}


	// Encodes a write or value request: the name, the mark '=' of a write form or none, and the fields. A string that is too long gives
	// invalidParameter, and nothing is queued.
	template<std::size_t Index, typename Arguments, bool Equals, typename Packed, std::size_t... K>
	AtStatus Fields(const Packed& packed, std::index_sequence<K...> sequence, Completion complete) {
		typename AtHandlerTupleOf<Arguments>::Type converted = AtConvertFields<Arguments>(packed, sequence);

		if (!AtFieldsFit<Arguments>(converted, sequence)) {
			return AtStatus{ResultStatus::invalidParameter};
		}

		AtTextBuffer<RequestChars> text;
		WriteName(text, Index);

		if constexpr (Equals) {
			text.Write(std::string_view("="));
		}

		AtWriteFields(text, converted, AtWrittenFields<Arguments>(converted, sequence), sequence);
		return Enqueue(text, Index, complete, false);
	}


	// Queues a request. It runs inside a critical section, and returns busy when the queue is full. A read request expects an info line.
	AtStatus Enqueue(const AtTextBuffer<RequestChars>& text, std::size_t command, Completion complete, bool expectsValue) {
		if (text.overflow) {
			return AtStatus{ResultStatus::invalidParameter};
		}

		System::CriticalSection(true);

		if (queueCount == QueueDepth) {
			System::CriticalSection(false);
			return AtStatus{ResultStatus::busy};
		}

		Request& slot = queue[(queueHead + queueCount) % QueueDepth];
		slot.length = text.length;
		std::copy_n(text.characters.data(), text.length, slot.text.data());
		slot.command = command;
		slot.complete = complete;
		slot.expectsValue = expectsValue;
		slot.state = RequestState::queued;
		slot.status = AtStatus{};
		slot.replyStored = false;
		queueCount++;

		System::CriticalSection(false);
		return AtStatus{};
	}


	// The handler of a Read form. It gets the stored value on success, and the status otherwise.
	template<AtText Name, auto Handler>
	static void CompleteRead(const std::byte* reply, AtStatus status) {
		using Replied = typename Set::template CommandOf<Name>::ReadType;

		if (status.status == ResultStatus::ok) {
			Handler(Result<Replied, AtStatus>(Ok<Replied>(ValueIn<Replied>(reply))));
		} else {
			Handler(Result<Replied, AtStatus>(Err<AtStatus>(status)));
		}
	}


	template<auto Handler>
	static void CompleteStatus(const std::byte* reply, AtStatus status) {
		static_cast<void>(reply);
		Handler(status);
	}


	// The request timeout of a command, with the set's default already applied.
	static uint32 TimeoutOf(std::size_t command) {
		return command < Set::CommandCount ? Set::commands[command].options.timeoutMs : Set::DefaultTimeoutMs;
	}


	// Writes the head request, and starts its timeout.
	void Send(Request& head, uint32 nowMs) {
		out.Write(std::string_view(head.text.data(), head.length));
		out.Write(std::string_view("\r\n"));
		head.state = RequestState::open;
		head.startMs = nowMs;
		echoPending = HasEcho;
	}


	// Sends the head request when it is queued and nothing is on the wire.
	void SendQueued(uint32 nowMs) {
		if (queueCount > 0 && queue[queueHead].state == RequestState::queued) {
			Send(queue[queueHead], nowMs);
		}
	}


	// Times out the open head, and ends the drain of a timed-out head when its drain time is up. The next request is sent after a
	// drain ends, and the loop goes on with it, since its own time has only just started.
	void Expire(uint32 nowMs) {
		while (queueCount > 0) {
			Request& head = queue[queueHead];

			if (head.state == RequestState::open) {
				if ((nowMs - head.startMs) < TimeoutOf(head.command)) {
					return;
				}

				head.state = RequestState::draining;
				head.status = AtStatus{ResultStatus::timeout};
				head.drainStartMs = nowMs;
				head.drainFinalSeen = false;
				out.Write(std::string_view("AT"));
				out.Write(std::string_view("\r\n"));
				return;
			}

			if (head.state != RequestState::draining || (nowMs - head.drainStartMs) < DrainTimeoutMs) {
				return;
			}

			FinishHead();
			SendQueued(nowMs);
		}
	}


	// Removes the head request, and runs its handler. The queue is changed first, so a handler can queue a request. The reply is read
	// before the handler runs, and the handler gets its own copy.
	void FinishHead() {
		Request& head = queue[queueHead];
		AtStatus outcome = head.status;

		if (outcome.status == ResultStatus::ok && !head.replyStored && head.expectsValue) {
			outcome = AtStatus{ResultStatus::invalidData};
		}

		Completion complete = head.complete;
		const std::byte* reply = head.reply;

		System::CriticalSection(true);
		queueHead = (queueHead + 1) % QueueDepth;
		queueCount--;
		System::CriticalSection(false);

		complete(reply, outcome);
	}


	void ConsumeByte(uint8 byte, uint32 nowMs) {
		char character = static_cast<char>(byte);

		if (character == '\r' || character == '\n') {
			char opposite = character == '\r' ? '\n' : '\r';

			if (lastTerminator == opposite) {
				lastTerminator = 0;
				return;
			}

			lastTerminator = character;
			EndLine(nowMs);
			return;
		}

		lastTerminator = 0;

		if (lineLength < LineChars) {
			line[lineLength] = character;
		}

		if (lineLength <= LineChars) {
			lineLength++;
		}
	}


	// A line is complete. An empty line is ignored. The next queued request is sent right after a line that finishes a request, so it is
	// sent at the same place in the byte stream, however the bytes were chunked.
	void EndLine(uint32 nowMs) {
		if (lineLength == 0) {
			return;
		}

		bool overflow = lineLength > LineChars;
		std::size_t stored = overflow ? LineChars : lineLength;
		HandleLine(std::string_view(line.data(), stored), overflow);
		lineLength = 0;
		SendQueued(nowMs);
	}


	void HandleLine(std::string_view text, bool overflow) {
		bool echo = echoPending;
		echoPending = false;

		if (echo && !overflow && queueCount > 0) {
			const Request& head = queue[queueHead];

			if (head.state == RequestState::open && text == std::string_view(head.text.data(), head.length)) {
				return;
			}
		}

		std::size_t urc = 0;
		std::size_t prefix = 0;

		if (MatchUrc(text, urc, prefix)) {
			HandleUrc(urc, text.substr(prefix), overflow);
			return;
		}

		if (queueCount == 0) {
			return;
		}

		Request& head = queue[queueHead];

		if (head.state == RequestState::draining) {
			DrainLine(head, text, overflow);
			return;
		}

		if (head.state != RequestState::open) {
			return;
		}

		if (InfoLine(head, text, overflow)) {
			return;
		}

		AtStatus result{};

		if (FinalLine(head.command, text, overflow, result)) {
			head.status = result;
			FinishHead();
		}
	}


	// A line while the head request is draining. The drain sent AT, so the module owes up to two finals: the late final of the timed-out
	// request, if the module answers it at all, and then the OK of AT. An OK cannot tell the two apart, so the first final line after the
	// timeout only moves the drain on, and the next OK ends it. A late OK therefore cannot end the drain early, and cannot answer the next
	// request. The cost: a module that answers AT alone ends the drain at its own timeout.
	void DrainLine(Request& head, std::string_view text, bool overflow) {
		if (overflow) {
			return;
		}

		if (!head.drainFinalSeen) {
			AtStatus ignored{};

			if (FinalLine(head.command, text, false, ignored)) {
				head.drainFinalSeen = true;
			}

			return;
		}

		if (text == "OK") {
			FinishHead();
		}
	}


	// True when the line starts with a URC prefix. The prefix ends at a colon, a comma or the end of the line. It is walked through the URC
	// root of the tree, one character at a time.
	bool MatchUrc(std::string_view text, std::size_t& urc, std::size_t& prefix) const {
		uint16 node = AtUrcRoot;
		std::size_t position = 0;

		for (; position < text.size(); position++) {
			char character = text[position];

			if (character == ':' || character == ',') {
				break;
			}

			node = CommandTreeStep(TreeSpan(), node, character);

			if (node == CommandNoNode) {
				return false;
			}
		}

		if (Set::tree[node].payload == CommandNoNode) {
			return false;
		}

		urc = Set::tree[node].payload;
		prefix = position;
		return true;
	}


	// The length of the reply prefix of a command in the line, or npos when the line is not its info line. A command without a read form
	// has no info line. Without a reply prefix the info line is the name and a colon.
	std::size_t InfoPrefixLength(std::size_t command, std::string_view text) const {
		if (command >= Set::CommandCount) {
			return std::string_view::npos;
		}

		const AtCommandInfo& info = Set::commands[command];

		if (!info.hasForm[AtFormIndex(AtFormKind::read)]) {
			return std::string_view::npos;
		}

		if (!info.options.replyPrefix.empty()) {
			return text.starts_with(info.options.replyPrefix) ? info.options.replyPrefix.size() : std::string_view::npos;
		}

		if (text.size() > info.name.size() && text.starts_with(info.name) && text[info.name.size()] == ':') {
			return info.name.size() + 1;
		}

		return std::string_view::npos;
	}


	// An info line of the head request. It is taken even when it does not parse, so an unknown line with its prefix is not read as a
	// final. The first value that parses is the reply; a bad one leaves it unstored, which the final line turns into invalidData.
	bool InfoLine(Request& head, std::string_view text, bool overflow) {
		if (!head.expectsValue) {
			return false;
		}

		std::size_t prefix = InfoPrefixLength(head.command, text);

		if (prefix == std::string_view::npos) {
			return false;
		}

		if (overflow) {
			return true;
		}

		std::string_view value = text.substr(prefix);

		if (!value.empty() && value[0] == ' ') {
			value.remove_prefix(1);
		}

		if (!head.replyStored) {
			head.replyStored = ParseReply(head.command, value, head.reply);
		}

		return true;
	}


	bool ParseReply(std::size_t command, std::string_view value, std::byte* reply) {
		bool parsed = false;

		VisitIndexed<Set::CommandCount>(command, [&]<std::size_t C>(std::integral_constant<std::size_t, C>) {
			parsed = ParseReplyOf<C>(value, reply);
		});

		return parsed;
	}


	template<std::size_t C>
	static bool ParseReplyOf(std::string_view value, std::byte* reply) {
		if constexpr (Set::commands[C].hasForm[AtFormIndex(AtFormKind::read)]) {
			using Replied = typename Set::template CommandType<C>::ReadType;
			using Reader = AtValueReader<Replied>;

			Reader reader{};

			for (char character : value) {
				reader.Push(character);
			}

			Replied parsed{};

			if (reader.Finish(parsed) != ResultStatus::ok) {
				return false;
			}

			::new (static_cast<void*>(reply)) Replied(parsed);
			return true;
		} else {
			static_cast<void>(value);
			static_cast<void>(reply);
			return false;
		}
	}


	// A final line of the head request. OK and ERROR are always finals. +CME ERROR: n gives the code, and a verbose text gives the code
	// of its entry in the dialect's table. The declared finals and failures of the command come last.
	bool FinalLine(std::size_t command, std::string_view text, bool overflow, AtStatus& status) const {
		if (overflow) {
			return false;
		}

		if (text == "OK") {
			status = AtStatus{ResultStatus::ok};
			return true;
		}

		if (text == "ERROR") {
			status = AtStatus{ResultStatus::error};
			return true;
		}

		if (text.starts_with("+CME ERROR:")) {
			status = CmeStatus(text.substr(11));
			return true;
		}

		if (command >= Set::CommandCount) {
			return false;
		}

		const AtCommandInfo& info = Set::commands[command];

		for (std::size_t index = 0; index < info.options.finalCount; index++) {
			if (text == info.options.finals[index]) {
				status = AtStatus{ResultStatus::ok};
				return true;
			}
		}

		for (std::size_t index = 0; index < info.options.failureCount; index++) {
			if (text == info.options.failures[index]) {
				status = AtStatus{ResultStatus::error};
				return true;
			}
		}

		return false;
	}


	// The status of the text after +CME ERROR:. A number is the code. A verbose text is looked up in the dialect's table. Any other text
	// is an error with no code.
	AtStatus CmeStatus(std::string_view rest) const {
		if (!rest.empty() && rest[0] == ' ') {
			rest.remove_prefix(1);
		}

		bool numeric = !rest.empty() && rest.size() <= 9;
		uint32 code = 0;

		for (char character : rest) {
			if (character < '0' || character > '9') {
				numeric = false;
				break;
			}

			code = code * 10 + static_cast<uint32>(character - '0');
		}

		if (numeric) {
			return AtCmeError(code > 65535 ? uint16{0} : static_cast<uint16>(code));
		}

		for (const AtCmeTextEntry& entry : Set::cmeTexts) {
			if (entry.text == rest) {
				return AtCmeError(entry.code);
			}
		}

		AtStatus status;
		status.status = ResultStatus::error;
		return status;
	}


	// A URC line: its arguments follow the prefix after a comma. A URC with no arguments fires on its prefix alone.
	void HandleUrc(std::size_t urc, std::string_view rest, bool overflow) {
		if (overflow) {
			return;
		}

		VisitIndexed<Set::UrcCount>(urc, [&]<std::size_t U>(std::integral_constant<std::size_t, U>) {
			DispatchUrc<U>(rest);
		});
	}


	// Parses the arguments of a URC and calls its handler. A line whose arguments do not parse is dropped, and the handler is not called.
	template<std::size_t U>
	void DispatchUrc(std::string_view rest) {
		using Arguments = typename Set::template UrcType<U>::ArgumentTypes;
		using Expected = typename AtUrcHandler<Arguments>::Type;
		constexpr std::size_t count = std::tuple_size_v<Arguments>;

		static_cast<void>(rest);
		Expected handler = AtPickHandler<Expected, typename Binding::Items>::Find(Set::urcNames[U]);

		if (handler == nullptr) {
			return;
		}

		if constexpr (count == 0) {
			handler();
		} else {
			if (!rest.empty() && rest[0] != ',') {
				return;
			}

			std::string_view fields = rest.empty() ? std::string_view{} : rest.substr(1);
			std::array<std::string_view, count> parts{};

			if (SplitFields(fields, parts) > count) {
				return;
			}

			typename AtStoredOf<Arguments>::Type values{};
			std::array<bool, count> present{};
			constexpr std::size_t base = Set::urcFieldBases[U];

			if (!ParseUrcFields<Arguments>(parts, values, present, base, std::make_index_sequence<count>{})) {
				return;
			}

			CallUrc<Arguments>(handler, values, present, std::make_index_sequence<count>{});
		}
	}


	// Splits the fields of a URC at commas, outside quotes. Returns the number of fields, and stops counting one past Count, which is more
	// fields than the URC declares.
	template<std::size_t Count>
	static std::size_t SplitFields(std::string_view text, std::array<std::string_view, Count>& parts) {
		std::size_t found = 0;
		std::size_t start = 0;
		bool quoted = false;
		bool escaped = false;

		for (std::size_t position = 0; position <= text.size(); position++) {
			if (position < text.size()) {
				char character = text[position];

				if (quoted) {
					if (escaped) {
						escaped = false;
					} else if (character == '\\') {
						escaped = true;
					} else if (character == '"') {
						quoted = false;
					}

					continue;
				}

				if (character == '"') {
					quoted = true;
					continue;
				}

				if (character != ',') {
					continue;
				}
			}

			if (found < Count) {
				parts[found] = text.substr(start, position - start);
			}

			found++;

			if (found > Count) {
				return found;
			}

			start = position + 1;
		}

		return found;
	}


	// One argument of a URC. An empty field is absent when the argument is optional, and a failure otherwise. A value that does not fit its
	// type, or an integer outside the range of its argument, is a failure too.
	template<typename Argument>
	static bool ParseUrcField(std::string_view field, typename Argument::Type& value, bool& present, const AtFieldRange& range) {
		if (field.empty()) {
			present = false;
			return Argument::isOptional;
		}

		using Reader = AtValueReader<typename Argument::Type>;

		Reader reader{};

		for (char character : field) {
			reader.Push(character);
		}

		if (reader.Finish(value) != ResultStatus::ok || !AtInRange(value, range)) {
			return false;
		}

		present = true;
		return true;
	}


	// The fields of a URC, each with the range of its argument. The ranges start at slot base of the set's URC arguments.
	template<typename Arguments, typename Stored, std::size_t... K>
	static bool ParseUrcFields(
		const std::array<std::string_view, sizeof...(K)>& parts,
		Stored& values,
		std::array<bool, sizeof...(K)>& present,
		std::size_t base,
		std::index_sequence<K...>)
	{
		bool parsed = true;

		((parsed = parsed && ParseUrcField<std::tuple_element_t<K, Arguments>>(parts[K], std::get<K>(values), present[K], Set::urcFieldRanges[base + K])), ...);
		return parsed;
	}


	// The handler's value of one URC argument: a view of a string, and std::nullopt for an absent optional argument.
	template<typename Argument>
	static typename Argument::HandlerType UrcArgument(const typename Argument::Type& value, bool present) {
		if constexpr (Argument::isOptional) {
			if (!present) {
				return {};
			}

			return typename Argument::HandlerType(AtHandlerValue(value));
		} else {
			static_cast<void>(present);
			return typename Argument::HandlerType(AtHandlerValue(value));
		}
	}


	template<typename Arguments, typename Stored, std::size_t... K>
	static void CallUrc(
		typename AtUrcHandler<Arguments>::Type handler,
		const Stored& values,
		const std::array<bool, sizeof...(K)>& present,
		std::index_sequence<K...>)
	{
		handler(UrcArgument<std::tuple_element_t<K, Arguments>>(std::get<K>(values), present[K])...);
	}
};
