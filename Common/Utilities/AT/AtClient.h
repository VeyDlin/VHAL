#pragma once
#include "AtArgument.h"
#include "AtBinding.h"
#include "AtCommandSet.h"
#include "AtDialect.h"
#include "AtError.h"
#include "AtForms.h"
#include "AtRow.h"
#include "AtValue.h"
#include "Modules/AtAwait.h"
#include "Modules/AtDataMode.h"
#include "Modules/AtEcho.h"
#include "Modules/AtRows.h"
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
// Run, Test, Value and the row calls) only queue a request, so they never write or wait. Feed() takes a byte into a ring, and may run in an
// interrupt. Process(nowMs) writes the request at the head of the queue, reads the bytes into lines, runs the handlers of the replies and of
// the URCs, and times the request out. The caller passes the time, so the client reads no clock.
//
// A request is one line on the wire, written at its head. Its reply is zero or more info lines, then a final line. The info lines are one
// value (Read<T>, Run<T>), one record (Read(row), Run(row)), or one row each (ReadRows, Rows, and their visits). A timed-out request is
// drained: the client sends AT and drops the lines. The first final line after the timeout only moves the drain on, and the next OK ends
// it, or the drain's own timeout does. Then the next request is sent. A late final line before the drain ends cannot answer the next
// request. A late OK after the drain ends can, since an OK cannot be told from the next request's OK.
//
// The line buffer keeps the first LineChars characters of a line. A line that is longer is never a URC or a final. An info line of a value
// that is too long gives invalidData. A record or row is read from the characters kept: every declared field lies inside them, so only the
// fields past the declared ones can be cut off, and those are skipped.
//
// A set that lists AtDataMode has two more reads of raw bytes. A prompt request takes the '>' that starts a line, and then writes its data.
// A Data() URC's header is the line up to its ':', and the bytes after the ':' are its run: they are read as chunks, not as lines, and the
// header stays in the line buffer until the run ends. Neither costs anything in a set without the module.
//
// A client that lists AtAwait has the Await calls. An Await call queues its request like the plain call, but no handler is called: when
// Process() finishes the request, its outcome and reply stay in the entry, which is then done. The waiting task copies the reply out and
// frees the entry. Await must run in a task, never in the Process() context. If Process() does not finish the request before the safety
// limit (the queue depth times the set's longest timeout plus the drain time), the wait ends with timeout and the entry is abandoned.
// Process() then frees it with no handler call, and does not send it when it is still queued.


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


template<std::size_t Capacity>
inline constexpr bool AtIsClientModule<AtRows<Capacity>> = true;


template<>
inline constexpr bool AtIsClientModule<AtAwait> = true;


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


// The prompt state of a request of a prompt command: the data written after the prompt, which is kept as a span and not copied, and
// whether the prompt has come.
struct AtPromptState {
	std::span<const uint8> payload{};
	bool sent = false;
};


// The prompt state of a client without AtDataMode. It takes no room.
struct AtNoPromptState {
};


// The receive state of a Data() URC: its chunk buffer, its URC, and the bytes left in the run. The blank after a prompt is skipped here too.
// Only a set that lists AtDataMode has it.
struct AtDataReceive {
	bool open = false;
	bool promptBlank = false;
	std::size_t urc = 0;
	uint32 left = 0;
	uint32 offset = 0;
	std::size_t chunkLength = 0;
	std::array<uint8, AtChunkBytes> chunk{};
};


// The receive state of a client without AtDataMode. It takes no room.
struct AtNoDataReceive {
};


// The await state of a request of an Await call: whether its waiter has given up at the safety limit, and the count of the rows of a rows
// reply, which the request copies from the client's row storage when it finishes.
struct AtAwaitEntry {
	bool abandoned = false;
	std::size_t rowCount = 0;
	std::size_t rowSkipped = 0;
};


// The await state of a client without AtAwait. It takes no room.
struct AtNoAwaitEntry {
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


// The longest request text of the set, over every form of every command: "AT", the name, the mark of a form and the fields. At least two,
// which is the bare AT of Ping, so a set with no command still has room for it.
template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxRequestChars(std::index_sequence<C...>) {
	return std::max({std::size_t{2}, (4 + Set::names[C].size() + AtFieldChars<typename Set::template CommandType<C>::FieldArguments>::value)...});
}


// The bytes of the largest single reply of a form: the value of a read or run form, or the record of one. A rows form is not stored in the
// request, and a form with no info line takes none.
template<typename Form>
constexpr std::size_t AtSingleReplyBytes() {
	if constexpr (std::is_void_v<Form>) {
		return 0;
	} else if constexpr (Form::shape == AtReplyShape::rows) {
		return 0;
	} else {
		return sizeof(typename Form::Type);
	}
}


// The bytes of the largest single reply of a command, over its read form and its run form.
template<typename Set, std::size_t C>
constexpr std::size_t AtReplyBytesOf() {
	using Command = typename Set::template CommandType<C>;
	return std::max(AtSingleReplyBytes<typename Command::ReadInfo>(), AtSingleReplyBytes<typename Command::RunInfo>());
}


template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxReplyBytes(std::index_sequence<C...>) {
	return std::max({std::size_t{1}, AtReplyBytesOf<Set, C>()...});
}


// The bytes of one row of a rows form, or zero for a form that is not rows.
template<typename Form>
constexpr std::size_t AtRowsFormBytes() {
	if constexpr (std::is_void_v<Form>) {
		return 0;
	} else if constexpr (Form::shape == AtReplyShape::rows) {
		return sizeof(typename Form::Type);
	} else {
		return 0;
	}
}


// The bytes of the largest row of a command, over its read form and its run form.
template<typename Set, std::size_t C>
constexpr std::size_t AtRowBytesOf() {
	using Command = typename Set::template CommandType<C>;
	return std::max(AtRowsFormBytes<typename Command::ReadInfo>(), AtRowsFormBytes<typename Command::RunInfo>());
}


// The bytes of the largest row of the set. At least one, so the storage is never empty.
template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxRowBytes(std::index_sequence<C...>) {
	return std::max({std::size_t{1}, AtRowBytesOf<Set, C>()...});
}


// The characters of an info line of one form: the prefix, one separator, and the value. For a record or rows the value is the Open text, the
// fields and the Close text.
template<typename Set, std::size_t C, bool Run, typename Form>
constexpr std::size_t AtFormChars() {
	if constexpr (std::is_void_v<Form>) {
		return 0;
	} else {
		constexpr std::size_t prefix = Set::commands[C].options.replyPrefix.empty() ? Set::names[C].size() + 1 : Set::commands[C].options.replyPrefix.size();

		if constexpr (Form::shape == AtReplyShape::value) {
			return prefix + 1 + AtValueChars<typename Form::Type>;
		} else {
			constexpr std::size_t slot = Set::template RowInfoIndex<C, Run>();
			constexpr std::size_t open = Set::rowInfos[slot].open.size();
			constexpr std::size_t close = Set::rowInfos[slot].close.size();
			return prefix + 1 + open + close + AtRowCharsOf<typename Form::Decl::FieldTypes>::value;
		}
	}
}


// The characters of an info line of a command, over its read form and its run form.
template<typename Set, std::size_t C>
constexpr std::size_t AtInfoChars() {
	using Command = typename Set::template CommandType<C>;
	return std::max(AtFormChars<Set, C, false, typename Command::ReadInfo>(), AtFormChars<Set, C, true, typename Command::RunInfo>());
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


// The longest timeout of the set: the set default, or the largest timeout that a command gives. The safety limit of an Await call is built from it.
template<typename Set>
constexpr uint32 AtLongestTimeoutMs() {
	uint32 longest = Set::DefaultTimeoutMs;

	for (const AtCommandInfo& command : Set::commands) {
		longest = std::max(longest, command.options.timeoutMs);
	}

	return longest;
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
	using Urc = typename Set::template UrcType<index>;
	static constexpr bool ok = std::is_same_v<typename Item::HandlerType, typename AtUrcHandler<typename Urc::ArgumentTypes, typename Urc::DataType>::Type>;

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
	static_assert((AtIsClientModule<Modules> && ...), "a client module is not an AtQueue or AtEcho or AtRows or AtAwait");
	static_assert(AtCountModule<AtEcho, Modules...>() <= 1, "AtEcho is listed twice in the client modules");
	static_assert(AtQueueCount<Modules...>() <= 1, "AtQueue is listed twice in the client modules");
	static_assert(AtRowsModuleCount<Modules...>() <= 1, "AtRows is listed twice in the client modules");
	static_assert(AtCountModule<AtAwait, Modules...>() <= 1, "AtAwait is listed twice in the client modules");
	static_assert(AtClientBindingCheck<Set, Binding>::ok, "a URC handler in the client binding does not match its description");

	static constexpr std::size_t QueueDepth = AtQueueDepthOf<Modules...>::value;

	static_assert(QueueDepth > 0, "an AT client queue needs a depth of at least 1");

	static constexpr bool HasEcho = AtCountModule<AtEcho, Modules...>() > 0;
	static constexpr bool HasRows = AtRowsModuleCount<Modules...>() > 0;
	static constexpr bool HasAwait = AtCountModule<AtAwait, Modules...>() > 0;
	static constexpr bool HasData = Set::HasData;
	static constexpr std::size_t RowsCapacity = AtRowsOf<Modules...>::capacity;
	static constexpr std::size_t RingSize = 64;
	static constexpr std::size_t RequestChars = AtMaxRequestChars<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t RowBytes = AtMaxRowBytes<Set>(std::make_index_sequence<Set::CommandCount>{});

	// An Await rows reply is copied into its entry, so a client with AwaitRows and AtAwait sizes every entry for the whole row storage.
	static constexpr std::size_t ReplyBytes = std::max(
		AtMaxReplyBytes<Set>(std::make_index_sequence<Set::CommandCount>{}),
		HasAwait ? RowsCapacity * RowBytes : std::size_t{0});
	static constexpr std::size_t LineChars = AtMaxLineCharsOf<Set>(std::make_index_sequence<Set::CommandCount>{}, std::make_index_sequence<Set::UrcCount>{});
	static constexpr uint32 DrainTimeoutMs = Set::DefaultTimeoutMs;

	// The longest an Await call waits: the queue depth times the set's longest timeout plus the drain time. A request can wait behind the
	// requests ahead of it, each of which takes at most that long.
	static constexpr uint32 AwaitLimitMs = static_cast<uint32>(QueueDepth) * (AtLongestTimeoutMs<Set>() + DrainTimeoutMs);

	// A completion runs the handler of a request with its outcome. The reply is the stored value or record, when one was stored.
	using Completion = void (*)(AtClient& client, const std::byte* reply, AtStatus status);

	// A parser reads the value of one info line into the place at base + index. A value is parsed into base; a row is parsed into the slot
	// of the row at index. A visit parser ignores both. It returns false when the line is not a value or row of the form.
	using ParseFunction = bool (*)(std::string_view value, bool overflow, std::byte* base, std::size_t index);

	// The info lines a request takes: none, one value or record, one row each, or a visit of each row.
	enum class ReplyKind : uint8 {
		none,
		single,
		rows,
		visit
	};


	// A done request is finished, and waits at the head of the queue for its Await call to copy its reply out.
	enum class RequestState : uint8 {
		queued,
		open,
		draining,
		done
	};


	// The info form of a command: its read form, or its run form. A command has at most one of each.
	template<std::size_t C, bool Run>
	using InfoOf = std::conditional_t<Run, typename Set::template CommandType<C>::RunInfo, typename Set::template CommandType<C>::ReadInfo>;


	// The data state of a set that lists AtDataMode, and the prompt state of a request of a prompt command in such a set.
	using DataStore = std::conditional_t<HasData, AtDataReceive, AtNoDataReceive>;
	using PromptStore = std::conditional_t<HasData, AtPromptState, AtNoPromptState>;
	using AwaitStore = std::conditional_t<HasAwait, AtAwaitEntry, AtNoAwaitEntry>;


	// One request: its text, the command, its handlers, and what the reply has given so far.
	struct Request {
		std::array<char, RequestChars> text{};
		std::size_t length = 0;
		std::size_t command = 0;
		Completion complete = nullptr;
		ParseFunction parse = nullptr;
		ReplyKind kind = ReplyKind::none;
		RequestState state = RequestState::queued;
		uint32 startMs = 0;
		uint32 drainStartMs = 0;
		bool drainFinalSeen = false;
		AtStatus status{};
		bool replyStored = false;
		bool replyBad = false;
		[[no_unique_address]] PromptStore prompt{};
		[[no_unique_address]] AwaitStore awaitState{};
		alignas(8) std::byte reply[ReplyBytes]{};
	};


	// The row storage of a client that lists AtRows: one list, shared by the calls. A client without it has none.
	using RowStore = std::conditional_t<HasRows, AtRowStorage<RowsCapacity * RowBytes>, AtNoRowStorage>;


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
	[[no_unique_address]] RowStore rows{};
	[[no_unique_address]] DataStore data{};


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


	// AT+X?. The handler gets the value or the record of the read form, or the status of the request.
	template<AtText Name, auto Handler>
	AtStatus Read() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(Command::readShape != AtReplyShape::rows, "AT command's read form is ReadRows: call ReadRows");
		static_assert(std::is_same_v<decltype(Handler), void (*)(Result<typename Command::ReadType, AtStatus>)>,
			"a Read handler is void(Result<T, AtStatus>), with T the type of the read form");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		return Enqueue<ReplyKind::single>(text, index, &CompleteValue<typename Command::ReadType, Handler>, InfoParser<index, false>());
	}


	// AT+X=a,b. The arguments are the handler types of the write form, and a string is any text of its capacity or less.
	template<AtText Name, auto Handler, typename... Values>
	AtStatus Write(Values... values) {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;
		using Arguments = typename Command::WriteArguments;

		static_assert(Command::template CountOf<AtFormKind::write>() == 1, "AT command has no write form");
		static_assert(!Set::commands[index].isPrompt, "a prompt command takes SendData, not Write");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Write handler is void(AtStatus)");
		static_assert(AtArgumentsAccept<Arguments, Values...>(), "Write is given values that do not match the arguments of its write form");

		return Fields<index, Arguments, true>(std::make_tuple(values...), std::index_sequence_for<Values...>{}, &CompleteStatus<Handler>);
	}


	// AT+X=a,b, then the data of the request after the module's prompt. The values are those of the write form, checked as Write checks them,
	// and the last value is the data, a std::span<const uint8>. The data is not copied: it must stay valid until the handler runs. The
	// handler is called with the final line of the request.
	template<AtText Name, auto Handler, typename... Values>
	AtStatus SendData(Values... values) {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;
		using Arguments = typename Command::WriteArguments;

		static_assert(HasData, "SendData needs AtDataMode among the modules of the set");
		static_assert(Command::template CountOf<AtFormKind::write>() == 1, "AT command has no write form");
		static_assert(Set::commands[index].isPrompt, "SendData is for a command with .Prompt()");
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a SendData handler is void(AtStatus)");
		static_assert(
			AtDataArgumentsAccept<Arguments, Values...>(),
			"SendData is given values that do not match the arguments of its write form, then the data as std::span<const uint8>"
		);

		if constexpr (AtDataArgumentsAccept<Arguments, Values...>()) {
			return SendDataCall<index, Arguments>(
				std::make_tuple(values...),
				std::make_index_sequence<sizeof...(Values) - 1>{},
				&CompleteStatus<Handler>);
		} else {
			return AtStatus{ResultStatus::invalidParameter};
		}
	}


	// AT+X. A run form with no info line is void(AtStatus). A run form with a value or a record is void(Result<T, AtStatus>).
	template<AtText Name, auto Handler>
	AtStatus Run() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(Command::runShape != AtReplyShape::rows, "AT command's run form is Rows: call Rows");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);

		if constexpr (Command::hasPlainRun) {
			static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Run handler is void(AtStatus)");
			return EnqueueNone(text, index, &CompleteStatus<Handler>);
		} else {
			static_assert(std::is_same_v<decltype(Handler), void (*)(Result<typename Command::RunType, AtStatus>)>,
				"a Run handler with a reply is void(Result<T, AtStatus>), with T the type of the run form");
			return Enqueue<ReplyKind::single>(text, index, &CompleteValue<typename Command::RunType, Handler>, InfoParser<index, true>());
		}
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
		return EnqueueNone(text, index, &CompleteStatus<Handler>);
	}


	// AT, the bare request that checks that the module answers. Its handler gets the status of the request, and it uses the set's default
	// timeout. A client that never calls Ping has no code for it.
	template<auto Handler>
	AtStatus Ping() {
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Ping handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		text.Write(std::string_view("AT"));
		return EnqueueNone(text, Set::CommandCount, &CompleteStatus<Handler>);
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


	// AT+X?, one info line for each row. The handler gets the rows that were stored, and the count of those past the capacity.
	template<AtText Name, auto Handler>
	AtStatus ReadRows() {
		static_assert(HasRows, "ReadRows needs the AtRows module in the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(Command::readShape == AtReplyShape::rows, "ReadRows is for a command whose read form is ReadRows(row)");
		static_assert(std::is_same_v<decltype(Handler), void (*)(const Result<AtRowList<typename Command::ReadType, RowsCapacity>, AtStatus>&)>,
			"a ReadRows handler is void(const Result<AtRowList<Row, N>, AtStatus>&)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		return Enqueue<ReplyKind::rows>(text, index, &CompleteRows<typename Command::ReadType, Handler>, InfoParser<index, false>());
	}


	// AT+X, one info line for each row. The handler gets the rows that were stored, and the count of those past the capacity.
	template<AtText Name, auto Handler>
	AtStatus Rows() {
		static_assert(HasRows, "Rows needs the AtRows module in the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(Command::runShape == AtReplyShape::rows, "Rows is for a command whose run form is Rows(row)");
		static_assert(std::is_same_v<decltype(Handler), void (*)(const Result<AtRowList<typename Command::RunType, RowsCapacity>, AtStatus>&)>,
			"a Rows handler is void(const Result<AtRowList<Row, N>, AtStatus>&)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		return Enqueue<ReplyKind::rows>(text, index, &CompleteRows<typename Command::RunType, Handler>, InfoParser<index, true>());
	}


	// AT+X?, one call of OnRow for each row as it arrives, and one call of OnDone with the status at the end. Nothing is stored.
	template<AtText Name, auto OnRow, auto OnDone>
	AtStatus ReadRowsVisit() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(Command::readShape == AtReplyShape::rows, "ReadRowsVisit is for a command whose read form is ReadRows(row)");
		static_assert(std::is_same_v<decltype(OnRow), void (*)(const typename Command::ReadType&)>, "a row handler is void(const Row&)");
		static_assert(std::is_same_v<decltype(OnDone), void (*)(AtStatus)>, "a done handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		return Enqueue<ReplyKind::visit>(text, index, &CompleteVisit<OnDone>, &ParseVisitInfo<index, false, OnRow>);
	}


	// AT+X, one call of OnRow for each row as it arrives, and one call of OnDone with the status at the end. Nothing is stored.
	template<AtText Name, auto OnRow, auto OnDone>
	AtStatus RowsVisit() {
		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(Command::runShape == AtReplyShape::rows, "RowsVisit is for a command whose run form is Rows(row)");
		static_assert(std::is_same_v<decltype(OnRow), void (*)(const typename Command::RunType&)>, "a row handler is void(const Row&)");
		static_assert(std::is_same_v<decltype(OnDone), void (*)(AtStatus)>, "a done handler is void(AtStatus)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		return Enqueue<ReplyKind::visit>(text, index, &CompleteVisit<OnDone>, &ParseVisitInfo<index, true, OnRow>);
	}


	// AwaitRead: Read, which waits for its reply and returns it, and the value is copied out. Needs AtAwait. Call it in a task, never in Process().
	template<AtText Name>
	Result<typename Set::template CommandOf<Name>::ReadType, AtStatus> AwaitRead() {
		static_assert(HasAwait, "AwaitRead needs AtAwait among the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(Command::readShape != AtReplyShape::rows, "AT command's read form is ReadRows: call AwaitReadRows");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		std::size_t slot = 0;
		AtStatus queued = AwaitQueue(
			[&]() -> AtStatus {
				return Enqueue<ReplyKind::single>(text, index, nullptr, InfoParser<index, false>());
			},
			slot);
		return AwaitValueOf<typename Command::ReadType>(queued, slot);
	}


	// AwaitRun: Run, which waits for its reply. A run form with no reply returns AtStatus; one with a value or a record returns its Result.
	template<AtText Name>
	std::conditional_t<
		Set::template CommandOf<Name>::hasPlainRun,
		AtStatus,
		Result<typename Set::template CommandOf<Name>::RunType, AtStatus>> AwaitRun()
	{
		static_assert(HasAwait, "AwaitRun needs AtAwait among the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(Command::runShape != AtReplyShape::rows, "AT command's run form is Rows: call AwaitRows");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		std::size_t slot = 0;

		if constexpr (Command::hasPlainRun) {
			AtStatus queued = AwaitQueue(
				[&]() -> AtStatus {
					return EnqueueNone(text, index, nullptr);
				},
				slot);
			return AwaitStatusOf(queued, slot);
		} else {
			AtStatus queued = AwaitQueue(
				[&]() -> AtStatus {
					return Enqueue<ReplyKind::single>(text, index, nullptr, InfoParser<index, true>());
				},
				slot);
			return AwaitValueOf<typename Command::RunType>(queued, slot);
		}
	}


	// AwaitPing: Ping, which waits for its final line and returns its status. Needs AtAwait. Call it in a task, never in Process().
	AtStatus AwaitPing() {
		static_assert(HasAwait, "AwaitPing needs AtAwait among the client modules");

		AtTextBuffer<RequestChars> text;
		text.Write(std::string_view("AT"));
		std::size_t slot = 0;
		AtStatus queued = AwaitQueue(
			[&]() -> AtStatus {
				return EnqueueNone(text, Set::CommandCount, nullptr);
			},
			slot);
		return AwaitStatusOf(queued, slot);
	}


	// AwaitWrite: Write, which waits for its final line. Needs AtAwait.
	template<AtText Name, typename... Values>
	AtStatus AwaitWrite(Values... values) {
		static_assert(HasAwait, "AwaitWrite needs AtAwait among the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;
		using Arguments = typename Command::WriteArguments;

		static_assert(Command::template CountOf<AtFormKind::write>() == 1, "AT command has no write form");
		static_assert(!Set::commands[index].isPrompt, "a prompt command takes AwaitSendData, not AwaitWrite");
		static_assert(AtArgumentsAccept<Arguments, Values...>(), "AwaitWrite is given values that do not match the arguments of its write form");

		std::size_t slot = 0;
		AtStatus queued = AwaitQueue(
			[&]() -> AtStatus {
				return Fields<index, Arguments, true>(std::make_tuple(values...), std::index_sequence_for<Values...>{}, nullptr);
			},
			slot);
		return AwaitStatusOf(queued, slot);
	}


	// AwaitValue: Value, a basic command or a builtin such as E, which waits for its final line. Needs AtAwait.
	template<AtText Name, typename... Values>
	AtStatus AwaitValue(Values... values) {
		static_assert(HasAwait, "AwaitValue needs AtAwait among the client modules");

		constexpr std::size_t treeIndex = Set::template TreeIndex<Name>();
		std::size_t slot = 0;

		if constexpr (treeIndex >= Set::CommandCount) {
			AtStatus queued = AwaitQueue(
				[&]() -> AtStatus {
					return BuiltinEnqueue<treeIndex>(nullptr, values...);
				},
				slot);
			return AwaitStatusOf(queued, slot);
		} else {
			using Command = typename Set::template CommandOf<Name>;
			using Arguments = typename Command::ValueArguments;

			static_assert(Command::isBasic, "a Value form is for a basic command, one letter");
			static_assert(Command::template CountOf<AtFormKind::value>() == 1, "AT command has no value form");
			static_assert(AtArgumentsAccept<Arguments, Values...>(), "AwaitValue is given values that do not match the arguments of its value form");

			AtStatus queued = AwaitQueue(
				[&]() -> AtStatus {
					return Fields<treeIndex, Arguments, false>(std::make_tuple(values...), std::index_sequence_for<Values...>{}, nullptr);
				},
				slot);
			return AwaitStatusOf(queued, slot);
		}
	}


	// AwaitReadRows: ReadRows, which waits for its rows. The rows are copied out. Needs AtAwait and AtRows.
	template<AtText Name>
	Result<AtRowCopy<typename Set::template CommandOf<Name>::ReadType, RowsCapacity>, AtStatus> AwaitReadRows() {
		static_assert(HasAwait, "AwaitReadRows needs AtAwait among the client modules");
		static_assert(HasRows, "AwaitReadRows needs the AtRows module in the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::read>() == 1, "AT command has no read form");
		static_assert(Command::readShape == AtReplyShape::rows, "AwaitReadRows is for a command whose read form is ReadRows(row)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		text.Write(std::string_view("?"));
		std::size_t slot = 0;
		AtStatus queued = AwaitQueue(
			[&]() -> AtStatus {
				return Enqueue<ReplyKind::rows>(text, index, nullptr, InfoParser<index, false>());
			},
			slot);
		return AwaitRowsOf<typename Command::ReadType>(queued, slot);
	}


	// AwaitRows: Rows, which waits for its rows. The rows are copied out. Needs AtAwait and AtRows.
	template<AtText Name>
	Result<AtRowCopy<typename Set::template CommandOf<Name>::RunType, RowsCapacity>, AtStatus> AwaitRows() {
		static_assert(HasAwait, "AwaitRows needs AtAwait among the client modules");
		static_assert(HasRows, "AwaitRows needs the AtRows module in the client modules");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;

		static_assert(Command::template CountOf<AtFormKind::run>() == 1, "AT command has no run form");
		static_assert(Command::runShape == AtReplyShape::rows, "AwaitRows is for a command whose run form is Rows(row)");

		AtTextBuffer<RequestChars> text;
		WriteName(text, index);
		std::size_t slot = 0;
		AtStatus queued = AwaitQueue(
			[&]() -> AtStatus {
				return Enqueue<ReplyKind::rows>(text, index, nullptr, InfoParser<index, true>());
			},
			slot);
		return AwaitRowsOf<typename Command::RunType>(queued, slot);
	}


	// AwaitSendData: SendData, which waits for the final line of the request. Needs AtAwait and AtDataMode. The data is not copied, as in SendData.
	template<AtText Name, typename... Values>
	AtStatus AwaitSendData(Values... values) {
		static_assert(HasAwait, "AwaitSendData needs AtAwait among the client modules");
		static_assert(HasData, "AwaitSendData needs AtDataMode among the modules of the set");

		constexpr std::size_t index = Set::template Index<Name>();
		using Command = typename Set::template CommandOf<Name>;
		using Arguments = typename Command::WriteArguments;

		static_assert(Command::template CountOf<AtFormKind::write>() == 1, "AT command has no write form");
		static_assert(Set::commands[index].isPrompt, "AwaitSendData is for a command with .Prompt()");
		static_assert(
			AtDataArgumentsAccept<Arguments, Values...>(),
			"AwaitSendData is given values that do not match the arguments of its write form, then the data as std::span<const uint8>"
		);

		if constexpr (AtDataArgumentsAccept<Arguments, Values...>()) {
			std::size_t slot = 0;
			AtStatus queued = AwaitQueue(
				[&]() -> AtStatus {
					return SendDataCall<index, Arguments>(std::make_tuple(values...), std::make_index_sequence<sizeof...(Values) - 1>{}, nullptr);
				},
				slot);
			return AwaitStatusOf(queued, slot);
		} else {
			return AtStatus{ResultStatus::invalidParameter};
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
		static_assert(std::is_same_v<decltype(Handler), void (*)(AtStatus)>, "a Value handler is void(AtStatus)");

		return BuiltinEnqueue<TreeIndex>(&CompleteStatus<Handler>, values...);
	}


	// Queues a builtin value with its completion, which is none for an Await call.
	template<std::size_t TreeIndex, typename... Values>
	AtStatus BuiltinEnqueue(Completion complete, Values... values) {
		constexpr AtBuiltin kind = Set::builtinList.kinds[TreeIndex - Set::CommandCount];

		static_assert(
			kind == AtBuiltin::echo || kind == AtBuiltin::verbose || kind == AtBuiltin::quiet,
			"Value is for the echo, verbose or quiet builtin, such as E"
		);
		static_assert(sizeof...(Values) == 1 && (std::is_same_v<Values, uint8> && ...), "a builtin takes one uint8 value, such as E0");

		AtTextBuffer<RequestChars> text;
		text.Write(std::string_view("AT"));
		text.Write(Set::treeNames[TreeIndex]);
		(AtWriteValue(text, values), ...);
		return EnqueueNone(text, TreeIndex, complete);
	}


	// The parser of the info line of a form: a value of a read or run form, or the row of a record or rows form. A command with no such form
	// has none.
	template<std::size_t C, bool Run>
	static ParseFunction InfoParser() {
		using Form = InfoOf<C, Run>;

		if constexpr (std::is_void_v<Form>) {
			return nullptr;
		} else if constexpr (Form::shape == AtReplyShape::value) {
			return &ParseValueInfo<typename Form::Type>;
		} else {
			return &ParseRecordInfo<C, Run>;
		}
	}


	// A value of a read or run form, read from the whole info line. A line that is too long is refused, since its value is cut.
	template<typename T>
	static bool ParseValueInfo(std::string_view value, bool overflow, std::byte* base, std::size_t index) {
		static_cast<void>(index);

		if (overflow) {
			return false;
		}

		AtValueReader<T> reader{};

		for (char character : value) {
			reader.Push(character);
		}

		T parsed{};

		if (reader.Finish(parsed) != ResultStatus::ok) {
			return false;
		}

		::new (static_cast<void*>(base)) T(parsed);
		return true;
	}


	// The row of a record or rows form, read with the fields and the Open and Close texts of the set's row table.
	template<std::size_t C, bool Run>
	static bool ReadRowOf(std::string_view value, bool overflow, typename InfoOf<C, Run>::Type& row) {
		constexpr std::size_t slot = Set::template RowInfoIndex<C, Run>();

		return AtReadRow<typename InfoOf<C, Run>::Decl>(
			value,
			overflow,
			Set::rowInfos[slot].open,
			Set::rowInfos[slot].close,
			Set::replyRanges.data() + Set::rowInfos[slot].rangeBase,
			row);
	}


	// A record, or one row of a rows form, written at its place: the base plus index times the row size.
	template<std::size_t C, bool Run>
	static bool ParseRecordInfo(std::string_view value, bool overflow, std::byte* base, std::size_t index) {
		using Row = typename InfoOf<C, Run>::Type;

		Row row{};

		if (!ReadRowOf<C, Run>(value, overflow, row)) {
			return false;
		}

		::new (static_cast<void*>(base + index * sizeof(Row))) Row(row);
		return true;
	}


	// One row of a visit: it is read, and the handler gets it. Nothing is stored.
	template<std::size_t C, bool Run, auto OnRow>
	static bool ParseVisitInfo(std::string_view value, bool overflow, std::byte* base, std::size_t index) {
		static_cast<void>(base);
		static_cast<void>(index);

		typename InfoOf<C, Run>::Type row{};

		if (!ReadRowOf<C, Run>(value, overflow, row)) {
			return false;
		}

		OnRow(row);
		return true;
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
		return EnqueueNone(text, Index, complete);
	}


	// Queues a request of the given kind. The kind is a constant at each call, so the wrapper is the only code each call adds.
	template<ReplyKind Kind>
	AtStatus Enqueue(const AtTextBuffer<RequestChars>& text, std::size_t command, Completion complete, ParseFunction parse) {
		return EnqueueOf(text, command, complete, parse, Kind);
	}


	// Queues a request with no info line. Its parser is none, so the call takes one argument fewer than an info request.
	AtStatus EnqueueNone(const AtTextBuffer<RequestChars>& text, std::size_t command, Completion complete) {
		return EnqueueOf(text, command, complete, nullptr, ReplyKind::none);
	}


	// Queues a request. It runs inside a critical section, and returns busy when the queue is full. A request with a single reply has none
	// stored yet; a request with no info line, or with rows or a visit, has nothing that can be missing.
	AtStatus EnqueueOf(const AtTextBuffer<RequestChars>& text, std::size_t command, Completion complete, ParseFunction parse, ReplyKind kind) {
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
		slot.parse = parse;
		slot.kind = kind;
		slot.state = RequestState::queued;
		slot.status = AtStatus{};
		slot.replyStored = kind != ReplyKind::single;
		slot.replyBad = false;

		if constexpr (HasAwait) {
			slot.awaitState = AwaitStore{};
		}

		queueCount++;

		System::CriticalSection(false);
		return AtStatus{};
	}


	// The handler of a value or record reply. It gets the stored value on success, and the status otherwise.
	template<typename Replied, auto Handler>
	static void CompleteValue(AtClient& client, const std::byte* reply, AtStatus status) {
		static_cast<void>(client);

		if (status.status == ResultStatus::ok) {
			Handler(Result<Replied, AtStatus>(Ok<Replied>(ValueIn<Replied>(reply))));
		} else {
			Handler(Result<Replied, AtStatus>(Err<AtStatus>(status)));
		}
	}


	// The handler of a rows reply. It gets the list of the stored rows, which views the client's row storage, or the status.
	template<typename Row, auto Handler>
	static void CompleteRows(AtClient& client, const std::byte* reply, AtStatus status) {
		static_cast<void>(reply);
		using List = AtRowList<Row, RowsCapacity>;

		if (status.status != ResultStatus::ok) {
			Handler(Result<List, AtStatus>(Err<AtStatus>(status)));
			return;
		}

		List list(client.rows.bytes, client.rows.count, client.rows.skipped);
		Handler(Result<List, AtStatus>(Ok<List>(list)));
	}


	// The end of a visit: the handler gets the status of the request, after the rows.
	template<auto OnDone>
	static void CompleteVisit(AtClient& client, const std::byte* reply, AtStatus status) {
		static_cast<void>(client);
		static_cast<void>(reply);
		OnDone(status);
	}


	template<auto Handler>
	static void CompleteStatus(AtClient& client, const std::byte* reply, AtStatus status) {
		static_cast<void>(client);
		static_cast<void>(reply);
		Handler(status);
	}


	// The request timeout of a command, with the set's default already applied.
	static uint32 TimeoutOf(std::size_t command) {
		return command < Set::CommandCount ? Set::commands[command].options.timeoutMs : Set::DefaultTimeoutMs;
	}


	// Writes one request line and its line end. The head request and the bare AT of a drain are written here, so they share the code.
	void WriteLine(std::string_view text) {
		out.Write(text);
		out.Write(std::string_view("\r\n"));
	}


	// Writes the head request, and starts its timeout. A rows reply starts with an empty list.
	void Send(Request& head, uint32 nowMs) {
		if constexpr (HasRows) {
			rows.count = 0;
			rows.skipped = 0;
		}

		WriteLine(std::string_view(head.text.data(), head.length));
		head.state = RequestState::open;
		head.startMs = nowMs;

		if constexpr (HasEcho) {
			echoPending = true;
		}
	}


	// Sends the head request when it is queued and nothing is on the wire.
	void SendQueued(uint32 nowMs) {
		if constexpr (HasAwait) {
			DropAbandonedQueued();
		}

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
				WriteLine(std::string_view("AT"));
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
	// before the handler runs, and the handler gets its own copy. A request whose reply is not stored (a single reply that never parsed,
	// or rows and a visit with a row that did not parse) ends with invalidData.
	void FinishHead() {
		Request& head = queue[queueHead];
		AtStatus outcome = head.status;

		if (outcome.status == ResultStatus::ok && !head.replyStored) {
			outcome = AtStatus{ResultStatus::invalidData};
		}

		if constexpr (HasAwait) {
			if (head.complete == nullptr) {
				FinishAwaited(head, outcome);
				return;
			}
		}

		Completion complete = head.complete;
		const std::byte* reply = head.reply;
		PopHead();

		complete(*this, reply, outcome);
	}


	// Removes the head request from the queue. The change is made in a critical section, so a request can be queued meanwhile.
	void PopHead() {
		System::CriticalSection(true);
		queueHead = (queueHead + 1) % QueueDepth;
		queueCount--;
		System::CriticalSection(false);
	}


	// Ends a request of an Await call. Its outcome stays in its entry, and the entry becomes done, so the waiting task copies the reply out and
	// frees it. An entry whose waiter has given up has nobody to wait for it, so it is freed here, with no handler call. Whether the waiter has
	// given up is read in the same critical section that marks the entry done, so one of the two always happens.
	void FinishAwaited(Request& head, AtStatus outcome) {
		if constexpr (HasRows) {
			if (head.kind == ReplyKind::rows) {
				std::copy_n(rows.bytes, RowsCapacity * RowBytes, head.reply);
				head.awaitState.rowCount = rows.count;
				head.awaitState.rowSkipped = rows.skipped;
			}
		}

		head.status = outcome;
		System::CriticalSection(true);

		if (head.awaitState.abandoned) {
			queueHead = (queueHead + 1) % QueueDepth;
			queueCount--;
		} else {
			head.state = RequestState::done;
		}

		System::CriticalSection(false);
	}


	// Removes the abandoned requests at the head of the queue that are still queued. Their waiter gave up before Process() sent them, so they
	// are not sent.
	void DropAbandonedQueued() {
		while (queueCount > 0 && queue[queueHead].state == RequestState::queued && queue[queueHead].awaitState.abandoned) {
			PopHead();
		}
	}


	// Queues a request of an Await call. The queue is held while the call runs, so the entry it adds is the tail when the call returns, and
	// slot receives its index. Returns the status of the queueing: busy when the queue is full.
	template<typename Call>
	AtStatus AwaitQueue(Call&& call, std::size_t& slot) {
		System::CriticalSection(true);
		AtStatus queued = call();

		if (queued.status == ResultStatus::ok) {
			slot = (queueHead + queueCount - 1) % QueueDepth;
		}

		System::CriticalSection(false);
		return queued;
	}


	// True when the request in slot is done. Process() writes the state, so it is read in a critical section.
	bool EntryDone(std::size_t slot) {
		System::CriticalSection(true);
		bool done = queue[slot].state == RequestState::done;
		System::CriticalSection(false);
		return done;
	}


	// Waits until the request in slot is done, or until the safety limit passes. Returns true when the request is done: the caller then owns the
	// entry, and frees it with PopHead. Returns false when the limit passed first: the entry is abandoned, and Process() frees it.
	bool AwaitDone(std::size_t slot) {
		ResultStatus waited = WaitCondition::WaitFor<WaitCondition::RtosYield>(
			[this, slot]() -> bool {
				return EntryDone(slot);
			},
			AwaitLimitMs);

		if (waited == ResultStatus::ok) {
			return true;
		}

		System::CriticalSection(true);
		bool done = queue[slot].state == RequestState::done;

		if (!done) {
			queue[slot].awaitState.abandoned = true;
		}

		System::CriticalSection(false);
		return done;
	}


	// The result of an Await call with no reply: the queueing status when it failed, else the timeout of the limit, else the request's status.
	AtStatus AwaitStatusOf(AtStatus queued, std::size_t slot) {
		if (queued.status != ResultStatus::ok) {
			return queued;
		}

		if (!AwaitDone(slot)) {
			return AtStatus{ResultStatus::timeout};
		}

		AtStatus status = queue[slot].status;
		PopHead();
		return status;
	}


	// The result of an Await call with one value or record. The stored reply is copied out before the entry is freed.
	template<typename Replied>
	Result<Replied, AtStatus> AwaitValueOf(AtStatus queued, std::size_t slot) {
		if (queued.status != ResultStatus::ok) {
			return Result<Replied, AtStatus>(Err<AtStatus>(queued));
		}

		if (!AwaitDone(slot)) {
			return Result<Replied, AtStatus>(Err<AtStatus>(AtStatus{ResultStatus::timeout}));
		}

		const Request& entry = queue[slot];
		Result<Replied, AtStatus> result = entry.status.status == ResultStatus::ok
			? Result<Replied, AtStatus>(Ok<Replied>(ValueIn<Replied>(entry.reply)))
			: Result<Replied, AtStatus>(Err<AtStatus>(entry.status));
		PopHead();
		return result;
	}


	// The result of an Await call of rows: a copy of the stored rows, which the request made when it finished.
	template<typename Row>
	Result<AtRowCopy<Row, RowsCapacity>, AtStatus> AwaitRowsOf(AtStatus queued, std::size_t slot) {
		using Copy = AtRowCopy<Row, RowsCapacity>;

		if (queued.status != ResultStatus::ok) {
			return Result<Copy, AtStatus>(Err<AtStatus>(queued));
		}

		if (!AwaitDone(slot)) {
			return Result<Copy, AtStatus>(Err<AtStatus>(AtStatus{ResultStatus::timeout}));
		}

		const Request& entry = queue[slot];
		Result<Copy, AtStatus> result = entry.status.status == ResultStatus::ok
			? Result<Copy, AtStatus>(Ok<Copy>(Copy(entry.reply, entry.awaitState.rowCount, entry.awaitState.rowSkipped)))
			: Result<Copy, AtStatus>(Err<AtStatus>(entry.status));
		PopHead();
		return result;
	}


	void ConsumeByte(uint8 byte, uint32 nowMs) {
		char character = static_cast<char>(byte);

		if constexpr (HasData) {
			if (data.open) {
				ConsumeDataByte(byte);
				return;
			}

			if (TakePrompt(character)) {
				return;
			}
		}

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

		if constexpr (HasData) {
			if (character == ':' && StartData()) {
				return;
			}
		}

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
		if constexpr (HasEcho) {
			bool echo = echoPending;
			echoPending = false;

			if (echo && !overflow && queueCount > 0) {
				const Request& head = queue[queueHead];

				if (head.state == RequestState::open && text == std::string_view(head.text.data(), head.length)) {
					return;
				}
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

		if constexpr (Set::HasFailureCode) {
			if (!overflow && head.kind == ReplyKind::none && FailureCodeLine(head, text)) {
				return;
			}
		}

		AtStatus result{};

		bool ended = false;

		if constexpr (HasData) {
			ended = IsPromptCommand(head.command) ? PromptFinalLine(head, text, overflow, result) : FinalLine(head.command, text, overflow, result);
		} else {
			ended = FinalLine(head.command, text, overflow, result);
		}

		if (ended) {
			if constexpr (Set::HasFailureCode) {
				KeepFailureCode(head, result);
			}

			head.status = result;
			FinishHead();
		}
	}


	// A failure code line of the head request: its prefix, a colon, one blank that is optional, and an unsigned integer that fits uint8.
	// The code is kept in the request's status until its final line. A later code line replaces an earlier one. A line with no integer, or
	// with one above 255, is not a failure code, so it is an unknown line. Only a request with no reply reads these lines: a request with a
	// reply reads them as its reply.
	static bool FailureCodeLine(Request& head, std::string_view text) {
		std::string_view prefix = AtFailureCodePrefixOf(Set::failureCodePrefixes, head.command);

		if (prefix.empty() || text.size() <= prefix.size() || !text.starts_with(prefix) || text[prefix.size()] != ':') {
			return false;
		}

		std::size_t position = prefix.size() + 1;

		if (position < text.size() && text[position] == ' ') {
			position++;
		}

		if (position >= text.size()) {
			return false;
		}

		uint32 code = 0;

		for (; position < text.size(); position++) {
			char character = text[position];

			if (character < '0' || character > '9') {
				return false;
			}

			code = code * 10 + static_cast<uint32>(character - '0');

			if (code > 255) {
				return false;
			}
		}

		head.status.failureCode = static_cast<uint8>(code);
		return true;
	}


	// The final line's status takes the failure code that the request read, only when the final is an error, so an OK never carries a code.
	static void KeepFailureCode(const Request& head, AtStatus& result) {
		if (result.status == ResultStatus::error) {
			result.failureCode = head.status.failureCode;
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


	// The length of the reply prefix of a command in the line, or npos when the line is not its info line. Without a reply prefix the info
	// line is the name and a colon.
	std::size_t InfoPrefixLength(std::size_t command, std::string_view text) const {
		if (command >= Set::CommandCount) {
			return std::string_view::npos;
		}

		const AtCommandInfo& info = Set::commands[command];

		if (!info.options.replyPrefix.empty()) {
			std::string_view reply = info.options.replyPrefix;

			if (!text.starts_with(reply)) {
				return std::string_view::npos;
			}

			// A prefix that ends in a letter or a digit ends a word, so +CIFSR:STAIP does not take the line +CIFSR:STAIP6LL.
			if constexpr (Set::HasCommaReplyPrefix) {
				if (text.size() > reply.size() && AtWordCharacter(reply.back()) && AtWordCharacter(text[reply.size()])) {
					return std::string_view::npos;
				}
			}

			return reply.size();
		}

		if (text.size() > info.name.size() && text.starts_with(info.name) && text[info.name.size()] == ':') {
			return info.name.size() + 1;
		}

		return std::string_view::npos;
	}


	// The start of the value of an info line, after its prefix: one comma when the reply prefix does not end in a colon, then one blank.
	std::size_t InfoValueStart(std::size_t command, std::string_view text, std::size_t prefix) const {
		std::size_t position = prefix;

		if constexpr (Set::HasCommaReplyPrefix) {
			std::string_view reply = Set::commands[command].options.replyPrefix;

			if (!reply.empty() && reply.back() != ':' && position < text.size() && text[position] == ',') {
				position++;
			}
		}

		if (position < text.size() && text[position] == ' ') {
			position++;
		}

		return position;
	}


	// An info line of the head request. It is taken even when it does not parse, so an unknown line with its prefix is not read as a
	// final. A single reply has one value line: a line that does not parse, or a second value line, makes it invalid, so a wrong value is
	// never handed over. A row or a visit that does not parse makes the reply invalid too.
	bool InfoLine(Request& head, std::string_view text, bool overflow) {
		if (head.kind == ReplyKind::none) {
			return false;
		}

		std::size_t prefix = InfoPrefixLength(head.command, text);

		if (prefix == std::string_view::npos) {
			return false;
		}

		std::string_view value = text.substr(InfoValueStart(head.command, text, prefix));

		switch (head.kind) {
			case ReplyKind::single:
				if (!head.replyBad) {
					if (!head.replyStored && head.parse(value, overflow, head.reply, 0)) {
						head.replyStored = true;
					} else {
						head.replyStored = false;
						head.replyBad = true;
					}
				}

				break;
			case ReplyKind::rows:
				StoreRow(head, value, overflow);
				break;
			case ReplyKind::visit:
				if (!head.parse(value, overflow, nullptr, 0)) {
					head.replyStored = false;
				}

				break;
			case ReplyKind::none:
				break;
		}

		return true;
	}


	// One row of a rows reply. The first RowsCapacity rows are stored, and the rest are counted as skipped. A row that does not parse makes
	// the reply invalid.
	void StoreRow(Request& head, std::string_view value, bool overflow) {
		if constexpr (HasRows) {
			if (rows.count < RowsCapacity) {
				if (head.parse(value, overflow, rows.bytes, rows.count)) {
					rows.count++;
				} else {
					head.replyStored = false;
				}
			} else {
				rows.skipped++;
			}
		} else {
			static_cast<void>(head);
			static_cast<void>(value);
			static_cast<void>(overflow);
		}
	}


	// A final line of the head request. OK and ERROR are always finals. +CME ERROR: n gives the code, and a verbose text gives the code of its
	// entry in the dialect's table. The declared finals and failures of the command come last.
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
		static_cast<void>(rest);

		// A Data() URC is read from its header and its raw bytes (StartData), never from a whole line, so a line of it is dropped here.
		if constexpr (Set::template UrcType<U>::hasData) {
			return;
		} else {
			DispatchPlainUrc<U>(rest);
		}
	}


	// The dispatch of a URC without Data(): its line's arguments are parsed, and its handler is called with them.
	template<std::size_t U>
	void DispatchPlainUrc(std::string_view rest) {
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


	template<typename Arguments, typename Handler, typename Stored, std::size_t... K>
	static void CallUrc(
		Handler handler,
		const Stored& values,
		const std::array<bool, sizeof...(K)>& present,
		std::index_sequence<K...>)
	{
		handler(UrcArgument<std::tuple_element_t<K, Arguments>>(std::get<K>(values), present[K])...);
	}


	// True when a command is a prompt command. A builtin is never one.
	bool IsPromptCommand(std::size_t command) const {
		return command < Set::CommandCount && Set::commands[command].isPrompt;
	}


	// The final line of a prompt request. It is a final when it is ERROR, +CME ERROR or a declared failure, before the prompt as well. A declared
	// final ends it only after the prompt, and OK never ends it, since the module sends OK before its prompt.
	bool PromptFinalLine(const Request& head, std::string_view text, bool overflow, AtStatus& status) const {
		if (overflow) {
			return false;
		}

		if (text == "ERROR") {
			status = AtStatus{ResultStatus::error};
			return true;
		}

		if (text.starts_with("+CME ERROR:")) {
			status = CmeStatus(text.substr(11));
			return true;
		}

		const AtCommandInfo& info = Set::commands[head.command];

		if (head.prompt.sent) {
			for (std::size_t index = 0; index < info.options.finalCount; index++) {
				if (text == info.options.finals[index]) {
					status = AtStatus{ResultStatus::ok};
					return true;
				}
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


	// Queues a request of a prompt command with its data. The data is kept as a span, which must stay valid until the handler runs. The
	// request has no info line, so its handler is void(AtStatus).
	AtStatus EnqueuePrompt(const AtTextBuffer<RequestChars>& text, std::size_t command, Completion complete, std::span<const uint8> payload) {
		static_cast<void>(payload);

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
		slot.parse = nullptr;
		slot.kind = ReplyKind::none;
		slot.state = RequestState::queued;
		slot.status = AtStatus{};
		slot.replyStored = true;

		if constexpr (HasData) {
			slot.prompt.payload = payload;
			slot.prompt.sent = false;
		}

		if constexpr (HasAwait) {
			slot.awaitState = AwaitStore{};
		}

		queueCount++;
		System::CriticalSection(false);
		return AtStatus{};
	}


	// Encodes the request of a SendData call, with the name, the mark and the values, and queues it with the data, which is the last value.
	template<std::size_t Index, typename Arguments, typename Packed, std::size_t... K>
	AtStatus SendDataCall(const Packed& packed, std::index_sequence<K...> sequence, Completion complete) {
		typename AtHandlerTupleOf<Arguments>::Type converted = AtConvertFields<Arguments>(packed, sequence);

		if (!AtFieldsFit<Arguments>(converted, sequence)) {
			return AtStatus{ResultStatus::invalidParameter};
		}

		AtTextBuffer<RequestChars> text;
		WriteName(text, Index);
		text.Write(std::string_view("="));
		AtWriteFields(text, converted, AtWrittenFields<Arguments>(converted, sequence), sequence);
		return EnqueuePrompt(text, Index, complete, std::get<sizeof...(K)>(packed));
	}


	// The prompt of a prompt request. A '>' at the start of a line, while the head request waits for its prompt, makes the client write the
	// data of the request, and the blank after the prompt is skipped. Returns true when the character is a part of the prompt, so it is not
	// read as a line.
	bool TakePrompt(char character) {
		if (data.promptBlank) {
			data.promptBlank = false;

			if (character == ' ') {
				lastTerminator = 0;
				return true;
			}
		}

		if (character != '>' || lineLength != 0 || queueCount == 0) {
			return false;
		}

		Request& head = queue[queueHead];

		if (head.state != RequestState::open || !IsPromptCommand(head.command) || head.prompt.sent) {
			return false;
		}

		head.prompt.sent = true;
		data.promptBlank = true;
		lastTerminator = 0;
		out.Write(std::string_view(reinterpret_cast<const char*>(head.prompt.payload.data()), head.prompt.payload.size()));
		return true;
	}


	// A line that is the header of a Data() URC, up to its ':', starts the raw bytes: the length is read here, and the bytes after the ':' are
	// the data. The header stays in the line buffer, since each chunk's handler call reads the header's arguments again. Returns false when the
	// line is not such a header, and then the ':' is an ordinary character of the line.
	bool StartData() {
		if (lineLength > LineChars) {
			return false;
		}

		std::string_view text(line.data(), lineLength);
		std::size_t urc = 0;
		std::size_t prefix = 0;

		if (!MatchUrc(text, urc, prefix)) {
			return false;
		}

		bool started = false;
		uint32 length = 0;

		VisitIndexed<Set::UrcCount>(urc, [&]<std::size_t U>(std::integral_constant<std::size_t, U>) {
			started = BeginData<U>(text.substr(prefix), length);
		});

		if (!started) {
			return false;
		}

		data.open = true;
		data.urc = urc;
		data.left = length;
		data.offset = 0;
		data.chunkLength = 0;

		if (length == 0) {
			FlushData();
		}

		return true;
	}


	// The header of a Data() URC, read after its prefix: its arguments, and the length, which is the last of them. Returns false when the
	// header does not parse, and then the URC has no data.
	template<std::size_t U>
	bool BeginData(std::string_view rest, uint32& length) {
		using Urc = typename Set::template UrcType<U>;

		if constexpr (Urc::hasData) {
			using Arguments = typename Urc::ArgumentTypes;
			constexpr std::size_t count = std::tuple_size_v<Arguments>;
			typename AtStoredOf<Arguments>::Type values{};

			if (!ReadUrcValues<U>(rest, values)) {
				return false;
			}

			length = static_cast<uint32>(std::get<count - 1>(values));
			return true;
		} else {
			static_cast<void>(rest);
			static_cast<void>(length);
			return false;
		}
	}


	// The arguments of a URC after its prefix: a comma, then the fields, which must be exactly the arguments and each must parse. The values
	// are written to values.
	template<std::size_t U, typename Stored>
	static bool ReadUrcValues(std::string_view rest, Stored& values) {
		using Arguments = typename Set::template UrcType<U>::ArgumentTypes;
		constexpr std::size_t count = std::tuple_size_v<Arguments>;

		if (rest.empty() || rest[0] != ',') {
			return false;
		}

		std::array<std::string_view, count> parts{};

		if (SplitFields(rest.substr(1), parts) != count) {
			return false;
		}

		std::array<bool, count> present{};
		return ParseUrcFields<Arguments>(parts, values, present, Set::urcFieldBases[U], std::make_index_sequence<count>{});
	}


	// One raw byte of a Data() run. The chunk is delivered when it is full, and with the last byte of the run.
	void ConsumeDataByte(uint8 byte) {
		data.chunk[data.chunkLength] = byte;
		data.chunkLength++;
		data.left--;

		if (data.chunkLength == AtChunkBytes || data.left == 0) {
			FlushData();
		}
	}


	// Delivers the chunk to the handler of the Data() URC, with the offset of its first byte. The last chunk ends the run, and then the line
	// buffer is cleared, so the lines after the run are read again.
	void FlushData() {
		bool last = data.left == 0;
		uint32 offset = data.offset;
		std::span<const uint8> chunk(data.chunk.data(), data.chunkLength);

		VisitIndexed<Set::UrcCount>(data.urc, [&]<std::size_t U>(std::integral_constant<std::size_t, U>) {
			CallDataUrc<U>(chunk, offset, last);
		});

		data.offset += static_cast<uint32>(data.chunkLength);
		data.chunkLength = 0;

		if (last) {
			data.open = false;
			lineLength = 0;
			lastTerminator = 0;
		}
	}


	// Calls the handler of a Data() URC with its arguments from the header, then the offset, the chunk and the last flag.
	template<std::size_t U>
	void CallDataUrc(std::span<const uint8> chunk, uint32 offset, bool last) {
		using Urc = typename Set::template UrcType<U>;

		if constexpr (Urc::hasData) {
			using Arguments = typename Urc::ArgumentTypes;
			using Expected = typename AtUrcHandler<Arguments, typename Urc::DataType>::Type;
			constexpr std::size_t count = std::tuple_size_v<Arguments>;

			Expected handler = AtPickHandler<Expected, typename Binding::Items>::Find(Set::urcNames[U]);

			if (handler == nullptr) {
				return;
			}

			typename AtStoredOf<Arguments>::Type values{};
			std::string_view header(line.data(), lineLength);

			if (!ReadUrcValues<U>(header.substr(Set::urcNames[U].size()), values)) {
				return;
			}

			CallDataHandler<Arguments>(
				handler,
				values,
				static_cast<typename Urc::DataType::Type>(offset),
				chunk,
				last,
				std::make_index_sequence<count>{});
		} else {
			static_cast<void>(chunk);
			static_cast<void>(offset);
			static_cast<void>(last);
		}
	}


	template<typename Arguments, typename Handler, typename Stored, typename Offset, std::size_t... K>
	static void CallDataHandler(Handler handler, const Stored& values, Offset offset, std::span<const uint8> chunk, bool last, std::index_sequence<K...>) {
		handler(UrcArgument<std::tuple_element_t<K, Arguments>>(std::get<K>(values), true)..., offset, chunk, last);
	}
};
