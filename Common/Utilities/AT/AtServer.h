#pragma once
#include "AtBinding.h"
#include "AtCommandSet.h"
#include "AtDialect.h"
#include "AtError.h"
#include "AtForms.h"
#include "AtLayout.h"
#include "AtRow.h"
#include "AtValue.h"
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


// The server: it reads AT command lines from bytes, runs the handlers of its description and writes the replies. Feed() takes a byte
// into a ring, and may run in an interrupt. Process() drains the ring, parses the bytes one character at a time, calls the handlers at
// the end of each command, and writes the replies and the queued URCs.
//
// The parser keeps no line. A command's name is a walk of the set's tree, one node per character. Each argument has a reader in one
// storage, which is started at the first character of the argument, and its value is kept in the value area at the argument's offset.
// A command runs at its ';' or at the end of the line, and each line gives one final result code. A line ends at CR or LF, and CRLF or
// LFCR is one end. With the registers a line ends at S3 instead. CR and LF are then ordinary characters, unless S3 is 13.
//
// A reply form writes its info lines after the command runs: one value (Read<T>, Run<T>), one record (Read(row), Run(row)), or one line for
// each row that the handler adds to its AtRowWriter (ReadRows, Rows).
//
// A prompt command has no handler: its line is answered with ERROR, since the server takes no data in version 1. A URC with Data() cannot be
// queued by the server, for the same reason.


// The field of one argument, by its command and its position. The server visits a field through a tag with these two numbers, so the
// reader and the value of each argument are instantiated for that argument only.
template<std::size_t Command, std::size_t Argument>
struct AtFieldTag {
	static constexpr std::size_t command = Command;
	static constexpr std::size_t argument = Argument;
};


// The owner command of each field in the set's numbering of fields.
template<std::size_t Count, std::size_t Total>
constexpr std::array<std::size_t, Total> AtFieldOwnersOf(const std::array<AtCommandInfo, Count>& infos) {
	std::array<std::size_t, Total> owners{};

	for (std::size_t command = 0; command < Count; command++) {
		for (std::size_t argument = 0; argument < infos[command].fieldCount; argument++) {
			owners[infos[command].fieldBase + argument] = command;
		}
	}

	return owners;
}


// The sizes of the storage that the server needs, from the largest command, reader and URC of the set.
template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxArgumentCount(std::index_sequence<C...>) {
	return std::max({Set::commands[C].fieldCount..., std::size_t{1}});
}


template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxValueBytes(std::index_sequence<C...>) {
	return std::max({AtLayoutOf<typename Set::template CommandType<C>::FieldArguments>::bytes..., std::size_t{1}});
}


template<typename Set, std::size_t... C>
constexpr std::size_t AtMaxReaderBytes(std::index_sequence<C...>) {
	return std::max({AtLayoutOf<typename Set::template CommandType<C>::FieldArguments>::readerBytes..., std::size_t{1}});
}


template<typename Set, std::size_t... U>
constexpr std::size_t AtMaxUrcBytes(std::index_sequence<U...>) {
	return std::max({AtLayoutOf<typename Set::template UrcType<U>::ArgumentTypes>::bytes..., std::size_t{1}});
}


// The compile-time checks of a binding against its description. Each check is a static_assert with its message.
template<typename Set, typename App, typename Binding>
struct AtBindingCheck {
	static constexpr std::size_t index = Set::template Index<Binding::nameText>();
	static constexpr bool ok = AtFitsForms<App, typename Binding::HandlerType, typename Set::template CommandType<index>::FormTypes>::value;
	static constexpr bool promptFree = !Set::commands[index].isPrompt;

	static_assert(ok, "an AT handler does not match a form of its command: check its parameters and its return type");
	static_assert(promptFree, "a prompt command has no server handler: the server answers its line with ERROR");
};


template<typename Set, typename App, typename Bindings>
struct AtBindingList;


template<typename Set, typename App, typename... Bindings>
struct AtBindingList<Set, App, AtServerBinding<Bindings...>> {
	static constexpr bool ok = (AtBindingCheck<Set, App, Bindings>::ok && ...);
};


// True when a form has the handler it needs: a test form needs none, a write form of a prompt command needs none (the server answers the
// line with ERROR), and each other form needs exactly one binding.
template<typename App, typename Form, typename... Bindings>
constexpr bool AtFormHandled(std::string_view name, bool prompt) {
	if constexpr (std::is_same_v<Form, AtTestForm>) {
		static_cast<void>(name);
		static_cast<void>(prompt);
		return true;
	} else if constexpr (Form::kind == AtFormKind::write) {
		return prompt || AtCountHandlers<typename AtFormHandler<App, Form>::Type, Bindings...>(name) == 1;
	} else {
		static_cast<void>(prompt);
		return AtCountHandlers<typename AtFormHandler<App, Form>::Type, Bindings...>(name) == 1;
	}
}


template<typename App, typename Forms, typename Binding>
struct AtFormsHandled;


template<typename App, typename... Forms, typename... Bindings>
struct AtFormsHandled<App, std::tuple<Forms...>, AtServerBinding<Bindings...>> {
	static constexpr bool Check(std::string_view name, bool prompt) {
		return (AtFormHandled<App, Forms, Bindings...>(name, prompt) && ...);
	}
};


template<typename Set, typename App, typename Binding, std::size_t... C>
constexpr bool AtCommandsHandled(std::index_sequence<C...>) {
	return (AtFormsHandled<App, typename Set::template CommandType<C>::FormTypes, Binding>::Check(Set::names[C], Set::commands[C].isPrompt) && ...);
}


// True when each value of a server URC has exactly the type that the URC stores for its argument. An int for a uint16 is refused, since
// the stored value would be cut without a word.
template<typename Arguments, typename Tuple, std::size_t... K>
constexpr bool AtUrcValuesMatch(std::index_sequence<K...>) {
	return (std::is_same_v<typename std::tuple_element_t<K, Arguments>::Type, std::tuple_element_t<K, Tuple>> && ...);
}


template<typename Arguments, typename... Values>
constexpr bool AtUrcValuesExact() {
	if constexpr (std::tuple_size_v<Arguments> != sizeof...(Values)) {
		return false;
	} else {
		return AtUrcValuesMatch<Arguments, std::tuple<Values...>>(std::make_index_sequence<sizeof...(Values)>{});
	}
}


template<typename Set, typename App, typename Binding, std::size_t UrcDepth = 4, std::size_t RingSize = 64>
class AtServer {
	static_assert(UrcDepth > 0, "a server URC queue needs a depth of at least 1");
	static_assert(AtBindingList<Set, App, Binding>::ok, "an AT handler does not match a form of its command");
	static_assert(
		AtCommandsHandled<Set, App, Binding>(std::make_index_sequence<Set::CommandCount>{}),
		"a command form has no server handler, or has two for one form"
	);

	template<std::size_t C>
	using LayoutOf = AtLayoutOf<typename Set::template CommandType<C>::FieldArguments>;

	template<std::size_t C, std::size_t K>
	using FieldArgumentOf = std::tuple_element_t<K, typename Set::template CommandType<C>::FieldArguments>;

	static constexpr std::size_t FieldTotal = Set::FieldTotal;
	static constexpr std::size_t MaxArguments = AtMaxArgumentCount<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t ValueBytes = AtMaxValueBytes<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t ReaderBytes = AtMaxReaderBytes<Set>(std::make_index_sequence<Set::CommandCount>{});
	static constexpr std::size_t UrcBytes = AtMaxUrcBytes<Set>(std::make_index_sequence<Set::UrcCount>{});
	static constexpr std::size_t BuiltinBytes = 8;
	static constexpr std::array<std::size_t, FieldTotal> fieldOwners = AtFieldOwnersOf<Set::CommandCount, FieldTotal>(Set::commands);

	// The queued URC: its index, and its arguments in a value area laid out as the URC's own.
	struct QueuedUrc {
		std::size_t urc = 0;
		alignas(8) std::byte values[UrcBytes]{};
	};


	// Where the parse is in the current command. A command is started by its name, and ends at ';' or at the end of the line.
	enum class Phase : uint8 {
		start,
		name,
		equals,
		pending,
		field,
		builtin
	};


	// The form that runs when a command's name and its marks are read, with no argument.
	enum class Pending : uint8 {
		none,
		read,
		test,
		run
	};


	CommandRing<RingSize> ring{};
	Print& out;
	App& app;
	uint32 droppedBytes = 0;
	uint8 prefixState = 0;
	bool lineHasData = false;
	bool failed = false;
	AtStatus failure{};
	char lastTerminator = 0;
	Phase phase = Phase::start;
	Pending pending = Pending::none;
	uint16 node = 0;
	std::size_t command = 0;
	std::size_t field = 0;
	bool fieldStarted = false;
	std::array<bool, MaxArguments> present{};
	alignas(8) std::byte readerBytes[ReaderBytes]{};
	alignas(8) std::byte values[ValueBytes]{};
	std::size_t builtinIndex = 0;
	std::array<char, BuiltinBytes> builtinText{};
	std::size_t builtinLength = 0;
	bool replyOpen = false;
	bool echoOn = true;
	bool verboseOn = true;
	bool quietOn = false;
	uint8 registerS3 = 13;
	uint8 registerS4 = 10;
	AtCmeMode cmeMode = Set::CmeMode;
	std::array<QueuedUrc, UrcDepth> queue{};
	std::size_t queueHead = 0;
	std::size_t queueCount = 0;


public:
	AtServer(App& application, Print& output) : out(output), app(application) {
	}


	// Takes a byte from the receive interrupt. Returns false, and the byte is dropped and counted, when the ring is full.
	bool Feed(uint8 byte) {
		return ring.Push(byte);
	}


	// Runs the bytes in the ring, and writes the queued URCs when no reply is open.
	void Process() {
		uint8 byte = 0;

		while (ring.Pop(byte)) {
			ConsumeByte(byte);
		}

		WriteQueuedUrcs();
	}


	// The bytes the ring has dropped since the server started.
	uint32 DroppedBytes() {
		droppedBytes += ring.TakeLost();
		return droppedBytes;
	}


	// Queues a URC with its arguments. Process writes it after the final line of the current command. Returns filled when the queue is full.
	template<TextLiteral Name, typename... Values>
	AtStatus Urc(Values... values) {
		constexpr std::size_t urc = Set::template UrcIndex<Name>();
		using Types = typename Set::template UrcType<urc>::ArgumentTypes;
		static_assert(!Set::template UrcType<urc>::hasData, "a server URC with Data() is not in v1: the server writes no raw data");
		static_assert(sizeof...(Values) == std::tuple_size_v<Types>, "a URC is given a different number of arguments than it declares");
		static_assert(AtUrcValuesExact<Types, Values...>(), "a URC is given values that do not match its arguments");

		QueuedUrc entry{};
		entry.urc = urc;
		StoreUrcValues<Types>(entry, std::index_sequence_for<Values...>{}, values...);

		System::CriticalSection(true);

		if (queueCount == UrcDepth) {
			System::CriticalSection(false);
			return AtStatus{ResultStatus::filled};
		}

		queue[(queueHead + queueCount) % UrcDepth] = entry;
		queueCount++;
		System::CriticalSection(false);
		return AtStatus{};
	}


private:
	// Calls visitor with the tag of each field. The visitor is a template lambda with the command and argument as its template parameters.
	template<typename Visitor>
	static void VisitField(std::size_t global, Visitor&& visitor) {
		static_cast<void>(global);
		static_cast<void>(visitor);

		[&]<std::size_t... I>(std::index_sequence<I...>) {
			((global == I ? (visitor(AtFieldTag<fieldOwners[I], I - Set::commands[fieldOwners[I]].fieldBase>{}), 0) : 0), ...);
		}(std::make_index_sequence<FieldTotal>{});
	}


	// Calls visitor with an integral constant for each index below Count, the one that equals index.
	template<std::size_t Count, typename Visitor>
	static void VisitIndexed(std::size_t index, Visitor&& visitor) {
		static_cast<void>(index);
		static_cast<void>(visitor);

		[&]<std::size_t... I>(std::index_sequence<I...>) {
			((index == I ? (visitor(std::integral_constant<std::size_t, I>{}), 0) : 0), ...);
		}(std::make_index_sequence<Count>{});
	}


	template<typename Expected>
	static Expected PickHandler(std::string_view name) {
		return AtPickHandler<Expected, typename Binding::Items>::Find(name);
	}


	static char ToUpper(char character) {
		return (character >= 'a' && character <= 'z') ? static_cast<char>(character - 'a' + 'A') : character;
	}


	static bool IsLetter(char character) {
		return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
	}


	// Reads 1 to maxDigits decimal digits. The value is changed only on success.
	static bool ParseDecimal(std::string_view text, std::size_t maxDigits, uint16& value) {
		if (text.empty() || text.size() > maxDigits) {
			return false;
		}

		uint16 result = 0;

		for (char character : text) {
			if (character < '0' || character > '9') {
				return false;
			}

			result = static_cast<uint16>(result * 10 + (character - '0'));
		}

		value = result;
		return true;
	}


	template<typename T>
	static const T& ValueIn(const std::byte* place) {
		return *std::launder(reinterpret_cast<const T*>(place));
	}


	template<std::size_t C, std::size_t K>
	void StartReader() {
		using Reader = AtValueReader<typename FieldArgumentOf<C, K>::Type>;
		::new (static_cast<void*>(readerBytes)) Reader();
	}


	template<std::size_t C, std::size_t K>
	bool PushReader(char character) {
		using Reader = AtValueReader<typename FieldArgumentOf<C, K>::Type>;
		return std::launder(reinterpret_cast<Reader*>(readerBytes))->Push(character);
	}


	template<std::size_t C, std::size_t K>
	bool ReaderQuoting() {
		using Reader = AtValueReader<typename FieldArgumentOf<C, K>::Type>;
		return std::launder(reinterpret_cast<Reader*>(readerBytes))->Quoting();
	}


	// Finishes the reader of the field and stores its value. Returns false when the text is not a value of the field's type, or an integer
	// outside the field's range. Both are refused the same way.
	template<std::size_t C, std::size_t K>
	bool FinishReader() {
		using Value = typename FieldArgumentOf<C, K>::Type;
		using Reader = AtValueReader<Value>;

		Value value{};
		ResultStatus status = std::launder(reinterpret_cast<Reader*>(readerBytes))->Finish(value);

		if (status != ResultStatus::ok || !AtInRange(value, Set::fieldRanges[Set::commands[C].fieldBase + K])) {
			return false;
		}

		::new (static_cast<void*>(values + LayoutOf<C>::offsets[K])) Value(value);
		return true;
	}


	template<std::size_t C, std::size_t K>
	typename FieldArgumentOf<C, K>::HandlerType LoadArgument() const {
		using Argument = FieldArgumentOf<C, K>;
		using Value = typename Argument::Type;

		if constexpr (Argument::isOptional) {
			if (!present[K]) {
				return {};
			}
		}

		return AtHandlerValue(ValueIn<Value>(values + LayoutOf<C>::offsets[K]));
	}


	template<std::size_t C, typename Handler, std::size_t... K>
	AtStatus CallArguments(Handler handler, std::index_sequence<K...>) {
		return handler(AtCall<App>{app}, LoadArgument<C, K>()...);
	}


	// Writes the range of the argument at the field's global index, which is its declared range.
	template<typename Arguments, std::size_t K>
	void WriteRangeAt(std::size_t global, bool separate) {
		using Value = typename std::tuple_element_t<K, Arguments>::Type;
		const AtFieldRange& range = Set::fieldRanges[global];

		if (separate) {
			WriteChar(',');
		}

		WriteChar('(');
		AtWriteRange<Value>(out, range.minimum, range.maximum);
		WriteChar(')');
	}


	template<typename Arguments, std::size_t... K>
	void WriteRangeList(std::size_t base, std::index_sequence<K...>) {
		((WriteRangeAt<Arguments, K>(base + K, K > 0)), ...);
	}


	template<typename Types, std::size_t K>
	void WriteUrcValue(const std::byte* area) {
		using Value = typename std::tuple_element_t<K, Types>::Type;

		WriteChar(',');
		AtWriteValue(out, ValueIn<Value>(area + AtLayoutOf<Types>::offsets[K]));
	}


	template<typename Types, std::size_t... K>
	void WriteUrcValues(const std::byte* area, std::index_sequence<K...>) {
		static_cast<void>(area);
		(WriteUrcValue<Types, K>(area), ...);
	}


	template<std::size_t U>
	void WriteUrc(const std::byte* area) {
		using Types = typename Set::template UrcType<U>::ArgumentTypes;

		WriteBreak();
		out.Write(Set::urcNames[U]);
		WriteUrcValues<Types>(area, std::make_index_sequence<std::tuple_size_v<Types>>{});
		WriteBreak();
	}


	template<typename Types, std::size_t K, typename Value>
	static void StoreUrcValue(QueuedUrc& entry, const Value& value) {
		using Stored = typename std::tuple_element_t<K, Types>::Type;

		::new (static_cast<void*>(entry.values + AtLayoutOf<Types>::offsets[K])) Stored(static_cast<Stored>(value));
	}


	template<typename Types, std::size_t... K, typename... Values>
	static void StoreUrcValues(QueuedUrc& entry, std::index_sequence<K...>, Values... values) {
		static_cast<void>(entry);
		(StoreUrcValue<Types, K>(entry, values), ...);
	}


	// The characters of an output line. S3 and S4 are the output line terminator and the line feed, which the registers change at run time.
	// S3 also ends an input line (see EndsLine).
	char S3() const {
		if constexpr (Set::template Has<AtRegistersItem>) {
			return static_cast<char>(registerS3);
		} else {
			return '\r';
		}
	}


	char S4() const {
		if constexpr (Set::template Has<AtRegistersItem>) {
			return static_cast<char>(registerS4);
		} else {
			return '\n';
		}
	}


	void WriteChar(char character) {
		out.Write(std::string_view(&character, 1));
	}


	void WriteBreak() {
		WriteChar(S3());
		WriteChar(S4());
	}


	void WriteThreeDigits(uint8 value) {
		char digits[3] = {static_cast<char>('0' + value / 100), static_cast<char>('0' + (value / 10) % 10), static_cast<char>('0' + value % 10)};
		out.Write(std::string_view(digits, 3));
	}


	// The start of the info line of a command: a line end, then the reply prefix and its separator. The prefix is its own, or the name
	// followed by a colon.
	void BeginCommandInfo(std::size_t index) {
		WriteBreak();

		if (Set::commands[index].options.replyPrefix.empty()) {
			out.Write(Set::names[index]);
			out.Write(std::string_view(":"));
		} else {
			out.Write(Set::commands[index].options.replyPrefix);
		}

		if constexpr (Set::HasCommaReplyPrefix) {
			std::string_view prefix = Set::commands[index].options.replyPrefix;
			out.Write(std::string_view(!prefix.empty() && prefix.back() != ':' ? "," : " "));
		} else {
			out.Write(std::string_view(" "));
		}

		replyOpen = true;
	}


	void BeginBuiltinInfo(std::string_view prefix) {
		WriteBreak();
		out.Write(prefix);
		out.Write(std::string_view(" "));
		replyOpen = true;
	}


	void EndInfo() {
		WriteBreak();
	}


	bool Verbose() const {
		if constexpr (Set::template Has<AtVerboseItem>) {
			return verboseOn;
		} else {
			return true;
		}
	}


	bool Quiet() const {
		if constexpr (Set::template Has<AtQuietItem>) {
			return quietOn;
		} else {
			return false;
		}
	}


	AtCmeMode CurrentCmeMode() const {
		if constexpr (Set::template Has<AtCmeCommandItem>) {
			return cmeMode;
		} else {
			return Set::CmeMode;
		}
	}


	std::string_view CmeTextOf(uint16 code) const {
		for (const AtCmeTextEntry& entry : Set::cmeTexts) {
			if (entry.code == code) {
				return entry.text;
			}
		}

		return {};
	}


	// The failure code line of the command that failed: its prefix, a colon and the code, then the end of the line. It is written only for an
	// error that carries a code, and only for a command with .FailureCode(). It is an info line, so Quiet does not stop it.
	void WriteFailureCode(const AtStatus& status) {
		if (status.status != ResultStatus::error || status.failureCode == 0) {
			return;
		}

		std::string_view prefix = AtFailureCodePrefixOf(Set::failureCodePrefixes, command);

		if (!prefix.empty()) {
			WriteBreak();
			out.Write(prefix);
			out.Write(std::string_view(":"));
			AtWriteValue(out, status.failureCode);
			EndInfo();
		}
	}


	// A result code: the word and the framing of the verbose form, or the number and one terminator of the numeric form.
	void WriteFinal(const AtStatus& status) {
		if (!Quiet()) {
			bool verbose = Verbose();

			if (verbose) {
				WriteBreak();
			}

			if (status.status == ResultStatus::ok) {
				out.Write(verbose ? std::string_view("OK") : std::string_view("0"));
			} else if (status.cmeCode != 0 && CurrentCmeMode() != AtCmeMode::off) {
				out.Write(std::string_view("+CME ERROR: "));
				std::string_view text = CurrentCmeMode() == AtCmeMode::verbose ? CmeTextOf(status.cmeCode) : std::string_view{};

				if (text.empty()) {
					AtWriteValue(out, status.cmeCode);
				} else {
					out.Write(text);
				}
			} else {
				out.Write(verbose ? std::string_view("ERROR") : std::string_view("4"));
			}

			if (verbose) {
				WriteBreak();
			} else {
				WriteChar(S3());
			}
		}

		replyOpen = false;
	}


	void Fail(const AtStatus& status) {
		if (!failed) {
			failed = true;
			failure = status;
		}
	}


	void Fail() {
		Fail(AtStatus{ResultStatus::error});
	}


	void ResetLine() {
		prefixState = 0;
		lineHasData = false;
		failed = false;
		failure = AtStatus{};
		phase = Phase::start;
		pending = Pending::none;
		field = 0;
		fieldStarted = false;
		builtinLength = 0;
		present.fill(false);
	}


	void EndLine() {
		if (!lineHasData) {
			return;
		}

		if (!failed) {
			if (prefixState < 2) {
				Fail();
			} else {
				FinishSegment();
			}
		}

		if constexpr (Set::HasFailureCode) {
			if (failed) {
				WriteFailureCode(failure);
			}
		}

		WriteFinal(failed ? failure : AtStatus{});
		ResetLine();
		WriteQueuedUrcs();
	}


	// True when a character ends an input line. Without the registers S3 is CR, so CR and LF end a line. With them, the line ends at S3, and
	// while S3 is 13 LF ends one too (V.250). A CR or LF that does not end the line is dropped (ConsumeByte).
	bool EndsLine(char character) const {
		return character == S3() || (S3() == '\r' && character == '\n');
	}


	// True when CR LF and LF CR are one line end. That holds while S3 is 13, which is the usual case, and always without the registers.
	bool PairsTerminators() const {
		return S3() == '\r';
	}


	void ConsumeByte(uint8 byte) {
		if constexpr (Set::template Has<AtEchoItem>) {
			if (echoOn) {
				WriteChar(static_cast<char>(byte));
			}
		}

		char character = static_cast<char>(byte);

		if (EndsLine(character)) {
			char opposite = character == '\r' ? '\n' : '\r';

			if (PairsTerminators() && lastTerminator == opposite) {
				lastTerminator = 0;
				return;
			}

			lastTerminator = PairsTerminators() ? character : 0;
			EndLine();
			return;
		}

		lastTerminator = 0;

		// A CR or LF that is not S3 is dropped, so a host that sends CR LF after S3 changed still gets its commands read.
		if constexpr (Set::template Has<AtRegistersItem>) {
			if (character == '\r' || character == '\n') {
				return;
			}
		}

		lineHasData = true;

		if (!failed) {
			ConsumeChar(character);
		}
	}


	void ConsumePrefix(char character) {
		if (prefixState == 0) {
			if (character == 'A' || character == 'a') {
				prefixState = 1;
			} else {
				Fail();
			}

			return;
		}

		if (character == 'T' || character == 't') {
			prefixState = 2;
		} else {
			Fail();
		}
	}


	void ConsumeChar(char character) {
		if (prefixState < 2) {
			ConsumePrefix(character);
			return;
		}

		// Inside quotes a comma or a semicolon is text of the argument.
		if (phase == Phase::field && fieldStarted && QuotingField()) {
			PushReaderCharacter(character);
			return;
		}

		if (character == ';') {
			FinishSegment();
			return;
		}

		switch (phase) {
			case Phase::start:
				StartSegment(character);
				return;
			case Phase::name:
				ContinueName(character);
				return;
			case Phase::equals:
				AfterEquals(character);
				return;
			case Phase::pending:
				Fail();
				return;
			case Phase::field:
				PushFieldCharacter(character);
				return;
			case Phase::builtin:
				AppendBuiltin(character);
				return;
		}
	}


	void StartSegment(char character) {
		if (character != '+' && !IsLetter(character)) {
			Fail();
			return;
		}

		uint16 next = CommandTreeStep(std::span<const CommandTreeNode>(Set::tree.data(), Set::tree.size()), 0, ToUpper(character));

		if (next == CommandNoNode) {
			Fail();
			return;
		}

		node = next;
		phase = Phase::name;
	}


	void ContinueName(char character) {
		uint16 next = CommandTreeStep(std::span<const CommandTreeNode>(Set::tree.data(), Set::tree.size()), node, ToUpper(character));

		if (next != CommandNoNode) {
			node = next;
			return;
		}

		EndName(character, true);
	}


	// The name is complete. The character after it, when there is one, decides the form: a basic command takes its value at once, and a
	// command takes =, ? or the end of the segment.
	void EndName(char character, bool hasCharacter) {
		uint16 payload = Set::tree[node].payload;

		if (payload == CommandNoNode) {
			Fail();
			return;
		}

		if (payload >= Set::CommandCount) {
			if constexpr (Set::BuiltinCount > 0) {
				builtinIndex = payload - Set::CommandCount;
				builtinLength = 0;
				phase = Phase::builtin;

				if (hasCharacter) {
					AppendBuiltin(character);
				}
			} else {
				Fail();
			}

			return;
		}

		command = payload;
		present.fill(false);
		const AtCommandInfo& info = Set::commands[command];

		if (info.isBasic) {
			if (!hasCharacter && info.hasForm[AtFormIndex(AtFormKind::run)]) {
				BeginPending(Pending::run);
				return;
			}

			if (!info.hasForm[AtFormIndex(AtFormKind::value)]) {
				Fail();
				return;
			}

			BeginField();

			if (hasCharacter) {
				PushFieldCharacter(character);
			}

			return;
		}

		if (!hasCharacter) {
			if (info.hasForm[AtFormIndex(AtFormKind::run)]) {
				BeginPending(Pending::run);
			} else {
				Fail();
			}

			return;
		}

		if (character == '?') {
			if (info.hasForm[AtFormIndex(AtFormKind::read)]) {
				BeginPending(Pending::read);
			} else {
				Fail();
			}

			return;
		}

		if (character == '=') {
			phase = Phase::equals;
			return;
		}

		Fail();
	}


	void BeginPending(Pending form) {
		pending = form;
		phase = Phase::pending;
	}


	void BeginField() {
		phase = Phase::field;
		field = 0;
		fieldStarted = false;
	}


	// After =: a ? is the test form, and anything else is the first argument of the write form.
	void AfterEquals(char character) {
		const AtCommandInfo& info = Set::commands[command];

		if (character == '?') {
			if (info.hasForm[AtFormIndex(AtFormKind::test)]) {
				BeginPending(Pending::test);
			} else {
				Fail();
			}

			return;
		}

		if (!info.hasForm[AtFormIndex(AtFormKind::write)]) {
			Fail();
			return;
		}

		BeginField();
		PushFieldCharacter(character);
	}


	// A comma ends the field and starts the next. A character of a field starts its reader at the field's first character.
	void PushFieldCharacter(char character) {
		const AtCommandInfo& info = Set::commands[command];

		if (character == ',') {
			FinishField();

			if (failed) {
				return;
			}

			field++;
			fieldStarted = false;

			if (field >= info.fieldCount) {
				Fail();
			}

			return;
		}

		if (!fieldStarted) {
			if (field >= info.fieldCount) {
				Fail();
				return;
			}

			BeginReader(info.fieldBase + field);
			fieldStarted = true;
		}

		PushReaderCharacter(character);
	}


	void BeginReader(std::size_t global) {
		VisitField(global, [&]<std::size_t C, std::size_t K>(AtFieldTag<C, K>) {
			StartReader<C, K>();
		});
	}


	void PushReaderCharacter(char character) {
		bool accepted = false;
		VisitField(FieldIndex(), [&]<std::size_t C, std::size_t K>(AtFieldTag<C, K>) {
			accepted = PushReader<C, K>(character);
		});

		if (!accepted) {
			Fail();
		}
	}


	bool QuotingField() {
		bool quoting = false;
		VisitField(FieldIndex(), [&]<std::size_t C, std::size_t K>(AtFieldTag<C, K>) {
			quoting = ReaderQuoting<C, K>();
		});
		return quoting;
	}


	bool FieldOptional(std::size_t global) {
		bool optional = false;
		VisitField(global, [&]<std::size_t C, std::size_t K>(AtFieldTag<C, K>) {
			optional = FieldArgumentOf<C, K>::isOptional;
		});
		return optional;
	}


	std::size_t FieldIndex() const {
		return Set::commands[command].fieldBase + field;
	}


	// Ends the current argument. An empty argument is absent when it is optional, and an error otherwise.
	void FinishField() {
		std::size_t global = FieldIndex();

		if (!fieldStarted) {
			if (!FieldOptional(global)) {
				Fail();
			}

			return;
		}

		bool done = false;
		VisitField(global, [&]<std::size_t C, std::size_t K>(AtFieldTag<C, K>) {
			done = FinishReader<C, K>();
		});

		if (done) {
			present[field] = true;
		} else {
			Fail();
		}
	}


	// Ends the last argument, and checks that every argument after it is optional.
	void FinishFields() {
		FinishField();

		if (failed) {
			return;
		}

		const AtCommandInfo& info = Set::commands[command];

		for (std::size_t index = field + 1; index < info.fieldCount; index++) {
			if (!FieldOptional(info.fieldBase + index)) {
				Fail();
				return;
			}
		}
	}


	// The value of a basic builtin runs to the next letter or plus, which starts the next command (V.250 concatenation: ATE0V1 is ATE0 and
	// then ATV1). A failed segment ends at its ';', so a letter in it is only skipped.
	void AppendBuiltin(char character) {
		if (IsLetter(character) || character == '+') {
			if (failed) {
				return;
			}

			ExecuteBuiltin();
			phase = Phase::start;
			StartSegment(character);
			return;
		}

		if (builtinLength >= builtinText.size()) {
			Fail();
			return;
		}

		builtinText[builtinLength] = character;
		builtinLength++;
	}


	// The end of a segment: ';' or the end of the line. It runs the command, unless the segment failed.
	void FinishSegment() {
		if (phase == Phase::name) {
			EndName(0, false);
		}

		if (!failed) {
			switch (phase) {
				case Phase::start:
				case Phase::name:
					break;
				case Phase::equals: {
					const AtCommandInfo& info = Set::commands[command];

					if (!info.hasForm[AtFormIndex(AtFormKind::write)]) {
						Fail();
						break;
					}

					BeginField();
					FinishFields();

					if (!failed) {
						ExecuteArguments();
					}

					break;
				}
				case Phase::pending:
					ExecutePending();
					break;
				case Phase::field:
					FinishFields();

					if (!failed) {
						ExecuteArguments();
					}

					break;
				case Phase::builtin:
					if constexpr (Set::BuiltinCount > 0) {
						ExecuteBuiltin();
					}

					break;
			}
		}

		phase = Phase::start;
	}


	void ExecutePending() {
		switch (pending) {
			case Pending::none:
				break;
			case Pending::read:
				ExecuteRead();
				break;
			case Pending::test:
				ExecuteTest();
				break;
			case Pending::run:
				ExecuteRun();
				break;
		}
	}


	void ExecuteRead() {
		VisitIndexed<Set::CommandCount>(command, [&]<std::size_t C>(std::integral_constant<std::size_t, C>) {
			RunRead<C>();
		});
	}


	void ExecuteTest() {
		VisitIndexed<Set::CommandCount>(command, [&]<std::size_t C>(std::integral_constant<std::size_t, C>) {
			RunTest<C>();
		});
	}


	void ExecuteRun() {
		VisitIndexed<Set::CommandCount>(command, [&]<std::size_t C>(std::integral_constant<std::size_t, C>) {
			RunRun<C>();
		});
	}


	void ExecuteArguments() {
		VisitIndexed<Set::CommandCount>(command, [&]<std::size_t C>(std::integral_constant<std::size_t, C>) {
			RunArguments<C>();
		});
	}


	// A reply form with an info line: a value, a record, or rows. Its handler is picked by its signature. A value or a record is written
	// as one info line after the handler returns. Rows are written by the handler, one info line for each row it adds to the writer.
	template<std::size_t C, typename Form>
	void RunInfoForm() {
		using Expected = typename AtFormHandler<App, Form>::Type;
		Expected handler = PickHandler<Expected>(Set::names[C]);

		if constexpr (Form::shape == AtReplyShape::rows) {
			using Row = typename Form::Type;
			constexpr std::size_t slot = Set::template RowInfoIndex<C, Form::kind == AtFormKind::run>();

			replyOpen = true;
			AtRowWriter<Row> writer(
				out,
				S3(),
				S4(),
				Set::names[C],
				Set::commands[C].options.replyPrefix,
				Set::rowInfos[slot].open,
				Set::rowInfos[slot].close,
				&AtWriteRow<typename Form::Decl>);
			AtStatus status = handler(AtCall<App>{app}, writer);

			if (status.status != ResultStatus::ok) {
				Fail(status);
			}
		} else {
			Result<typename Form::Type, AtStatus> result = handler(AtCall<App>{app});

			if (result.IsErr()) {
				Fail(result.Error());
				return;
			}

			BeginCommandInfo(C);

			if constexpr (Form::shape == AtReplyShape::value) {
				AtWriteValue(out, result.Value());
			} else {
				constexpr std::size_t slot = Set::template RowInfoIndex<C, Form::kind == AtFormKind::run>();
				AtWriteRow<typename Form::Decl>(out, result.Value(), Set::rowInfos[slot].open, Set::rowInfos[slot].close);
			}

			EndInfo();
		}
	}


	template<std::size_t C>
	void RunRead() {
		if constexpr (!std::is_void_v<typename Set::template CommandType<C>::ReadInfo>) {
			RunInfoForm<C, typename Set::template CommandType<C>::ReadInfo>();
		}
	}


	// The test reply: the prefix, then the range of each argument of the write form. A command with no arguments has no test reply.
	template<std::size_t C>
	void RunTest() {
		using Arguments = typename Set::template CommandType<C>::FieldArguments;

		if constexpr (std::tuple_size_v<Arguments> > 0) {
			BeginCommandInfo(C);
			WriteRangeList<Arguments>(Set::commands[C].fieldBase, std::make_index_sequence<std::tuple_size_v<Arguments>>{});
			EndInfo();
		}
	}


	// A run form: a plain one runs its handler, and a run form with an info line runs its handler and writes its info line.
	template<std::size_t C>
	void RunRun() {
		using Command = typename Set::template CommandType<C>;

		if constexpr (Command::hasPlainRun) {
			using Expected = typename AtFormHandler<App, AtRunForm>::Type;

			Expected handler = PickHandler<Expected>(Set::names[C]);
			AtStatus status = handler(AtCall<App>{app});

			if (status.status != ResultStatus::ok) {
				Fail(status);
			}
		} else if constexpr (!std::is_void_v<typename Command::RunInfo>) {
			RunInfoForm<C, typename Command::RunInfo>();
		}
	}


	template<std::size_t C>
	void RunArguments() {
		using Arguments = typename Set::template CommandType<C>::FieldArguments;

		if constexpr (Set::commands[C].hasForm[AtFormIndex(AtFormKind::write)] || Set::commands[C].hasForm[AtFormIndex(AtFormKind::value)]) {
			if constexpr (Set::commands[C].isPrompt) {
				// A prompt command has no handler in v1, so its line is answered with ERROR.
				Fail();
			} else {
				using Expected = typename AtArgumentsHandler<App, Arguments>::Type;

				Expected handler = PickHandler<Expected>(Set::names[C]);
				AtStatus status = CallArguments<C>(handler, std::make_index_sequence<std::tuple_size_v<Arguments>>{});

				if (status.status != ResultStatus::ok) {
					Fail(status);
				}
			}
		}
	}


	// The builtin commands of the dialect. Each one reads its text, which is the characters after its name.
	void ExecuteBuiltin() {
		if constexpr (Set::BuiltinCount > 0) {
			std::string_view text(builtinText.data(), builtinLength);
			AtBuiltin kind = Set::builtinList.kinds[builtinIndex];

			switch (kind) {
				case AtBuiltin::echo:
					if constexpr (Set::template Has<AtEchoItem>) {
						SetSwitch(text, echoOn);
					}

					break;
				case AtBuiltin::verbose:
					if constexpr (Set::template Has<AtVerboseItem>) {
						SetSwitch(text, verboseOn);
					}

					break;
				case AtBuiltin::quiet:
					if constexpr (Set::template Has<AtQuietItem>) {
						SetSwitch(text, quietOn);
					}

					break;
				case AtBuiltin::registers:
					if constexpr (Set::template Has<AtRegistersItem>) {
						ExecuteRegister(text);
					}

					break;
				case AtBuiltin::cmee:
					if constexpr (Set::template Has<AtCmeCommandItem>) {
						ExecuteCmee(text);
					}

					break;
			}
		}
	}


	// ATE, ATV and ATQ: 0 or 1. An empty value is 0, as V.250 gives.
	void SetSwitch(std::string_view text, bool& flag) {
		if (text.empty() || text == "0") {
			flag = false;
		} else if (text == "1") {
			flag = true;
		} else {
			Fail();
		}
	}


	// ATS3? and ATS4?: the register as three digits, in an info line. ATS3=n sets it, with n from 0 to 127 (V.250).
	void ExecuteRegister(std::string_view text) {
		std::size_t split = text.find_first_of("?=");

		if (split == std::string_view::npos) {
			Fail();
			return;
		}

		uint16 number = 0;

		if (!ParseDecimal(text.substr(0, split), 3, number) || (number != 3 && number != 4)) {
			Fail();
			return;
		}

		uint8& slot = number == 3 ? registerS3 : registerS4;
		std::string_view rest = text.substr(split);

		if (rest == "?") {
			WriteBreak();
			WriteThreeDigits(slot);
			WriteBreak();
			replyOpen = true;
			return;
		}

		uint16 value = 0;

		if (rest.size() < 2 || rest[0] != '=' || !ParseDecimal(rest.substr(1), 3, value) || value > 127) {
			Fail();
			return;
		}

		slot = static_cast<uint8>(value);
	}


	// AT+CMEE=n sets the CME mode, 0 for ERROR, 1 for the number and 2 for the text. AT+CMEE? reads it, and AT+CMEE=? gives the range.
	void ExecuteCmee(std::string_view text) {
		if (text == "=?") {
			BeginBuiltinInfo("+CMEE:");
			out.Write(std::string_view("(0-2)"));
			EndInfo();
			return;
		}

		if (text == "?") {
			BeginBuiltinInfo("+CMEE:");
			AtWriteValue(out, static_cast<uint8>(cmeMode));
			EndInfo();
			return;
		}

		uint16 value = 0;

		if (text.size() >= 2 && text[0] == '=' && ParseDecimal(text.substr(1), 1, value) && value <= 2) {
			cmeMode = static_cast<AtCmeMode>(value);
			return;
		}

		Fail();
	}


	// Writes each queued URC, when no reply is open and no line is being read. Each entry is taken out under the critical section, and
	// written outside it.
	void WriteQueuedUrcs() {
		if (replyOpen || lineHasData) {
			return;
		}

		QueuedUrc entry{};

		while (TakeUrc(entry)) {
			VisitIndexed<Set::UrcCount>(entry.urc, [&]<std::size_t U>(std::integral_constant<std::size_t, U>) {
				WriteUrc<U>(entry.values);
			});
		}
	}


	bool TakeUrc(QueuedUrc& entry) {
		System::CriticalSection(true);
		bool taken = queueCount > 0;

		if (taken) {
			entry = queue[queueHead];
			queueHead = (queueHead + 1) % UrcDepth;
			queueCount--;
		}

		System::CriticalSection(false);
		return taken;
	}
};
