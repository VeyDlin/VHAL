#include "CommandSim.h"
#include <Utilities/CommandLine/CommandLine.h>
#include <cstddef>
#include <cstdio>
#include <string>


// The console, fed one byte at a time through Feed and Process, as the application does. Each expected text comes from the design: the
// echo, the messages and the prompt. The Backspace tests compare a line typed with an erased junk part against the same line typed
// directly, since Backspace must undo the junk exactly.


namespace {


enum class TraceModeValue : uint8 {
	Fast,
	Slow
};


// The application of the tests. Each handler and event appends to log, and each command sets its own fields.
struct TraceApp {
	std::string log;
	int keyCode = -1;
	int moveX = 0;
	int moveY = 0;
	bool moveFast = false;
	int mode = -1;
	std::string label;
	int rpm = 0;
	int stops = 0;
	int gain = 0;
	int interrupts = 0;
	unsigned long wide = 0;
	long long big = 0;
	unsigned long long wideValue = 0;
	int words = 0;
	int cutWords = 0;
};


const char* KindName(CommandEventKind kind) {
	switch (kind) {
		case CommandEventKind::Command:
			return "Command";
		case CommandEventKind::CommandFailed:
			return "CommandFailed";
		case CommandEventKind::UnknownCommand:
			return "UnknownCommand";
		case CommandEventKind::ArgumentError:
			return "ArgumentError";
		case CommandEventKind::EmptyLine:
			return "EmptyLine";
		case CommandEventKind::Word:
			return "Word";
		case CommandEventKind::EndOfLine:
			return "EndOfLine";
		case CommandEventKind::Interrupt:
			return "Interrupt";
		case CommandEventKind::TerminalOpened:
			return "TerminalOpened";
		case CommandEventKind::TerminalClosed:
			return "TerminalClosed";
		case CommandEventKind::InputOverflow:
			return "InputOverflow";
	}

	return "?";
}


// One event as the log reads it: kind|text|command|argument|fault|status, then a line end.
std::string Describe(const CommandEvent& event) {
	std::string text = KindName(event.kind);
	text += '|';
	text += std::string(event.text);
	text += '|';
	text += std::string(event.command);
	text += '|';
	text += std::string(event.argument);
	text += '|';
	text += std::to_string(static_cast<int>(event.fault));
	text += '|';
	text += std::to_string(static_cast<int>(event.status));
	text += '\n';
	return text;
}


// The log line the tests expect for an event, built from the same fields.
std::string Ev(const char* kind, const std::string& text = "", const std::string& command = "", const std::string& argument = "", int fault = 0, int status = 0) {
	return std::string(kind) + '|' + text + '|' + command + '|' + argument + '|' + std::to_string(fault) + '|' + std::to_string(status) + '\n';
}


ResultStatus TraceKey(CommandCall<TraceApp> call, uint8 code) {
	call.app.keyCode = code;
	return ResultStatus::ok;
}


ResultStatus TraceMove(CommandCall<TraceApp> call, int8 x, int8 y, bool fast) {
	call.app.moveX = x;
	call.app.moveY = y;
	call.app.moveFast = fast;
	return ResultStatus::ok;
}


ResultStatus TraceMode(CommandCall<TraceApp> call, TraceModeValue mode, String<8> label) {
	call.app.mode = static_cast<int>(mode);
	call.app.label = std::string(label.View());
	return ResultStatus::ok;
}


ResultStatus TraceFail(CommandCall<TraceApp> call) {
	static_cast<void>(call);
	return ResultStatus::invalidParameter;
}


ResultStatus TraceRange(CommandCall<TraceApp> call, uint32 wide, int64 big) {
	call.app.wide = wide;
	call.app.big = big;
	return ResultStatus::ok;
}


ResultStatus TraceSpeed(CommandCall<TraceApp> call, int16 rpm) {
	call.app.rpm = rpm;
	return ResultStatus::ok;
}


ResultStatus TraceStop(CommandCall<TraceApp> call) {
	call.app.stops++;
	return ResultStatus::ok;
}


ResultStatus TraceGain(CommandCall<TraceApp> call, int32 value) {
	call.app.gain = value;
	return ResultStatus::ok;
}


ResultStatus TraceTake(CommandCall<TraceApp> call, uint64 value) {
	call.app.wideValue = value;
	return ResultStatus::ok;
}


void TraceCountWords(CommandCall<TraceApp> call, const CommandEvent& event) {
	if (event.kind == CommandEventKind::Word) {
		call.app.words++;

		if (event.truncated) {
			call.app.cutWords++;
		}
	}
}


void TraceEvent(CommandCall<TraceApp> call, const CommandEvent& event) {
	call.app.log += Describe(event);
}


void TraceInterrupt(TraceApp& app) {
	app.interrupts++;
}


using TracePid = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("gain", "set the gain", &TraceGain, Argument<int32>("value").Range(-1000, 1000));
}>;


using TraceMotor = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("speed", "set the speed", &TraceSpeed, Argument<int16>("rpm").Range(-1000, 1000).Default(0))
		.Command("stop", "stop the motor", &TraceStop)
		.Group<TracePid>("pid", "pid tuning");
}>;


// The full set: a Default() takes every event, and OnInterrupt runs at once.
using TraceCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &TraceKey, Argument<uint8>("code"))
		.Command("move", "move the pointer", &TraceMove, Argument<int8>("x").Range(-127, 127), Argument<int8>("y").Range(-127, 127), Argument<bool>("fast").Default(false))
		.Command("mode", "set a mode", &TraceMode, Argument<TraceModeValue>("mode").Values({{"fast", TraceModeValue::Fast}, {"slow", TraceModeValue::Slow}}), Argument<String<8>>("label").Default("none"))
		.Command("fail", "always fails", &TraceFail)
		.Command("a_b-9", "a name with symbols", &TraceKey, Argument<uint8>("code"))
		.Group<TraceMotor>("motor", "motor control")
		.OnInterrupt(&TraceInterrupt)
		.Default(&TraceEvent);
}>;


// The same commands with no Default(): the standard messages are printed.
using PlainCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &TraceKey, Argument<uint8>("code"))
		.Command("move", "move the pointer", &TraceMove, Argument<int8>("x").Range(-127, 127), Argument<int8>("y").Range(-127, 127), Argument<bool>("fast").Default(false))
		.Command("mode", "set a mode", &TraceMode, Argument<TraceModeValue>("mode").Values({{"fast", TraceModeValue::Fast}, {"slow", TraceModeValue::Slow}}), Argument<String<8>>("label").Default("none"))
		.Command("fail", "always fails", &TraceFail)
		.Command("a_b-9", "a name with symbols", &TraceKey, Argument<uint8>("code"))
		.Group<TraceMotor>("motor", "motor control");
}>;


// The same commands with OnInterrupt and no Default().
using PlainInterrupt = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &TraceKey, Argument<uint8>("code"))
		.Group<TraceMotor>("motor", "motor control")
		.OnInterrupt(&TraceInterrupt);
}>;


// Integer ranges at the edges of their types.
using RangeCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("rng", "a range", &TraceRange, Argument<uint32>("wide").Range(0U, 4000000000U), Argument<int64>("big"));
}>;


// A full-width unsigned value, whose maximum does not fit in int64.
using WideCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("take", "take a wide value", &TraceTake, Argument<uint64>("v"));
}>;


// Default-only, and the default counts the words it is given.
using CountingWords = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&TraceCountWords);
}>;


// Default-only: no commands, so every event is a word event.
using TraceWords = CommandSet<+[] {
	return CommandSetBuilder()
		.Default(&TraceEvent);
}>;


// Default-only with no Default(): the standard handler, which prints nothing for words.
using PlainWords = CommandSet<+[] {
	return CommandSetBuilder();
}>;


std::string Summary(const TraceApp& app) {
	std::string text = "key=" + std::to_string(app.keyCode);
	text += " move=" + std::to_string(app.moveX) + "," + std::to_string(app.moveY) + "," + std::to_string(app.moveFast);
	text += " mode=" + std::to_string(app.mode) + " label=[" + app.label + "]";
	text += " rpm=" + std::to_string(app.rpm) + " stops=" + std::to_string(app.stops) + " gain=" + std::to_string(app.gain);
	text += " interrupts=" + std::to_string(app.interrupts) + " wide=" + std::to_string(app.wide) + " big=" + std::to_string(app.big);
	text += " log=[" + app.log + "]";
	return text;
}


// Text with its control characters shown, for a failure message.
std::string Escaped(const std::string& text) {
	std::string shown;
	for (char character : text) {
		if (character == '\r') {
			shown += "\\r";
		} else if (character == '\n') {
			shown += "\\n";
		} else if (character == '\b') {
			shown += "\\b";
		} else {
			shown += character;
		}
	}

	return shown;
}


void ExpectText(const std::string& actual, const std::string& expected, const char* file, int line) {
	if (actual != expected) {
		std::fprintf(stderr, "%s:%d: text differs\n  actual:   [%s]\n  expected: [%s]\n", file, line, Escaped(actual).c_str(), Escaped(expected).c_str());
		CommandSim::Expect(false, "text", file, line);
	}
}


#define CommandExpectText(actual, expected) ExpectText((actual), (expected), __FILE__, __LINE__)


// The echo of a line as a terminal shows it: a Backspace echo removes the last character.
std::string Visible(const std::string& echo) {
	std::string line;

	for (std::size_t index = 0; index < echo.size(); index++) {
		if (echo.compare(index, 3, "\b \b") == 0) {
			if (!line.empty()) {
				line.pop_back();
			}

			index += 2;
		} else {
			line += echo[index];
		}
	}

	return line;
}


// The text the console printed for one line: the echo up to its line end, then the messages and the prompt.
struct LineResult {
	std::string echo;
	std::string messages;
};


LineResult SplitLine(const std::string& output) {
	LineResult result;
	std::size_t end = output.find("\r\n");

	if (end == std::string::npos) {
		result.echo = output;
		return result;
	}

	result.echo = output.substr(0, end);
	result.messages = output.substr(end + 2);
	return result;
}


template<typename Set>
class Session {
public:
	TraceApp app;
	CommandSim::RecordingPrint out;
	CommandLine<Set, TraceApp> console;


	Session() : console(app, out) {
	}


	// Feeds each byte and processes it before the next, as a main loop reading a serial line does.
	void Type(const std::string& text) {
		for (char character : text) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}
	}


	std::string Take() {
		std::string text = out.Text();
		out.Clear();
		return text;
	}
};


// Types a line that ends with CR, and returns what the console printed for it.
template<typename Set>
std::string Run(Session<Set>& session, const std::string& line) {
	session.Type(line + "\r");
	return session.Take();
}


void TestEcho() {
	Session<PlainCommands> session;
	session.console.SetTerminalOpen(true);
	session.console.Process();
	CommandExpectText(session.Take(), "VHAL command line\r\n> ");

	// Each character is echoed as it arrives. Enter echoes the line end, runs the command, and the prompt comes back.
	session.Type("key 5");
	CommandExpectText(session.Take(), "key 5");
	session.Type("\r");
	CommandExpectText(session.Take(), "\r\n> ");
	CommandExpect(session.app.keyCode == 5);

	// A Backspace erases the echoed character, and a typed line shows what is left.
	session.app.keyCode = -1;
	session.Type("kex\b\b\bkey 5\r");
	CommandExpectText(session.Take(), "kex\b \b\b \b\b \bkey 5\r\n> ");
	CommandExpect(session.app.keyCode == 5);

	// Backspace at the start of an empty line does nothing and echoes nothing.
	session.Type("\b\b");
	CommandExpectText(session.Take(), "");

	// Tab is a space, and DEL erases like BS.
	session.app.keyCode = -1;
	session.Type("key\t5");
	session.Type(std::string(1, '\x7f'));
	session.Type("5\r");
	CommandExpectText(session.Take(), "key 5\b \b5\r\n> ");
	CommandExpect(session.app.keyCode == 5);
}


void TestLineEnds() {
	struct Case {
		const char* input;
		int lines;
	};

	const Case cases[] = {
		{"\r", 1},
		{"\n", 1},
		{"\r\n", 1},
		{"\n\r", 1},
		{"\r\r", 2},
		{"\n\n", 2},
		{"\r\n\r\n", 2},
		{"\n\r\r", 2},
		{"\r\n\n", 2}
	};

	for (const Case& item : cases) {
		Session<PlainCommands> session;
		session.Type(item.input);

		std::string expected;
		for (int line = 0; line < item.lines; line++) {
			expected += "\r\n> ";
		}

		CommandExpectText(session.Take(), expected);
	}
}


void TestRanges() {
	Session<PlainCommands> session;

	CommandExpectText(Run(session, "key 255"), "key 255\r\n> ");
	CommandExpect(session.app.keyCode == 255);

	CommandExpectText(Run(session, "key 256"), "key 256\r\nkey: code must be 0..255\r\n> ");
	CommandExpectText(Run(session, "key 0x1ff"), "key 0x1ff\r\nkey: code must be 0..255\r\n> ");
	CommandExpectText(Run(session, "key -1"), "key -1\r\nkey: code must be a number\r\n> ");

	// int8 edges: -127 and 127 are accepted, -128 and 128 are not.
	CommandExpectText(Run(session, "move -127 127"), "move -127 127\r\n> ");
	CommandExpect(session.app.moveX == -127);
	CommandExpect(session.app.moveY == 127);
	CommandExpect(!session.app.moveFast);

	CommandExpectText(Run(session, "move -128 0"), "move -128 0\r\nmove: x must be -127..127\r\n> ");
	CommandExpectText(Run(session, "move 1 128"), "move 1 128\r\nmove: y must be -127..127\r\n> ");
	CommandExpectText(Run(session, "move 200 1"), "move 200 1\r\nmove: x must be -127..127\r\n> ");

	CommandExpectText(Run(session, "move 127 -127 on"), "move 127 -127 on\r\n> ");
	CommandExpect(session.app.moveFast);

	// An int16 with a Range and a Default: the edges are inside, and past them is refused.
	CommandExpectText(Run(session, "motor speed -1000"), "motor speed -1000\r\n> ");
	CommandExpect(session.app.rpm == -1000);
	CommandExpectText(Run(session, "motor speed 1001"), "motor speed 1001\r\nspeed: rpm must be -1000..1000\r\n> ");

	// uint32 and int64 edges.
	Session<RangeCommands> range;
	CommandExpectText(Run(range, "rng 4000000000 -9223372036854775808"), "rng 4000000000 -9223372036854775808\r\n> ");
	CommandExpect(range.app.wide == 4000000000UL);
	CommandExpect(range.app.big == -9223372036854775807LL - 1);

	CommandExpectText(Run(range, "rng 4000000001 0"), "rng 4000000001 0\r\nrng: wide must be 0..4000000000\r\n> ");
	CommandExpectText(Run(range, "rng 0 9223372036854775808"), "rng 0 9223372036854775808\r\nrng: big must be -9223372036854775808..9223372036854775807\r\n> ");
	CommandExpectText(Run(range, "rng 0 9223372036854775807"), "rng 0 9223372036854775807\r\n> ");
	CommandExpect(range.app.big == 9223372036854775807LL);
}


void TestCommandsAndMessages() {
	Session<PlainCommands> session;

	// An unknown word, and a prefix that is no command, both print the unknown command message.
	CommandExpectText(Run(session, "xyz"), "xyz\r\nunknown command 'xyz', try help\r\n> ");
	CommandExpectText(Run(session, "ke"), "ke\r\nunknown command 'ke', try help\r\n> ");
	CommandExpectText(Run(session, "keyx 1"), "keyx 1\r\nunknown command 'keyx', try help\r\n> ");

	// A group name alone is not a command, and neither is a name the group does not have.
	CommandExpectText(Run(session, "motor"), "motor\r\nunknown command 'motor', try help\r\n> ");
	CommandExpectText(Run(session, "motor xyz"), "motor xyz\r\nunknown command 'xyz', try help\r\n> ");
	CommandExpectText(Run(session, "motor pid"), "motor pid\r\nunknown command 'pid', try help\r\n> ");

	// A handler that returns a status other than ok raises CommandFailed, with the status name.
	CommandExpectText(Run(session, "fail"), "fail\r\nfail: invalidParameter\r\n> ");

	// Missing and extra arguments.
	CommandExpectText(Run(session, "key"), "key\r\nkey: code is missing\r\n> ");
	CommandExpectText(Run(session, "key 1 2"), "key 1 2\r\nkey: too many arguments\r\n> ");
	CommandExpectText(Run(session, "fail 1"), "fail 1\r\nfail: too many arguments\r\n> ");
	CommandExpectText(Run(session, "motor pid gain 5 6"), "motor pid gain 5 6\r\ngain: too many arguments\r\n> ");

	// Enum and bool arguments name their values.
	CommandExpectText(Run(session, "mode medium"), "mode medium\r\nmode: mode must be a listed value\r\n> ");
	CommandExpectText(Run(session, "move 1 2 maybe"), "move 1 2 maybe\r\nmove: fast must be on, off, true, false, 1 or 0\r\n> ");
	CommandExpectText(Run(session, "move 1 2 o"), "move 1 2 o\r\nmove: fast must be on, off, true, false, 1 or 0\r\n> ");
	CommandExpectText(Run(session, "move 1 2 1"), "move 1 2 1\r\n> ");
	CommandExpect(session.app.moveFast);
	CommandExpectText(Run(session, "mode slow"), "mode slow\r\n> ");
	CommandExpect(session.app.mode == 1);
	CommandExpect(session.app.label == "none");

	// Groups: a command inside a group, and a command inside a group inside a group.
	CommandExpectText(Run(session, "motor stop"), "motor stop\r\n> ");
	CommandExpect(session.app.stops == 1);
	CommandExpectText(Run(session, "motor pid gain 250"), "motor pid gain 250\r\n> ");
	CommandExpect(session.app.gain == 250);
	CommandExpectText(Run(session, "motor pid gain 1001"), "motor pid gain 1001\r\ngain: value must be -1000..1000\r\n> ");
	CommandExpectText(Run(session, "motor speed"), "motor speed\r\n> ");
	CommandExpect(session.app.rpm == 0);

	// Names with symbols.
	CommandExpectText(Run(session, "a_b-9 1"), "a_b-9 1\r\n> ");
	CommandExpect(session.app.keyCode == 1);

	// An empty line, and a line of spaces only, are both empty.
	CommandExpectText(Run(session, ""), "\r\n> ");
	CommandExpectText(Run(session, "   "), "   \r\n> ");
}


void TestStrings() {
	Session<PlainCommands> session;

	CommandExpectText(Run(session, "mode slow \"a b\""), "mode slow \"a b\"\r\n> ");
	CommandExpect(session.app.label == "a b");

	// An escaped quote and an escaped backslash inside quotes.
	CommandExpectText(Run(session, "mode slow \"a\\\"b\""), "mode slow \"a\\\"b\"\r\n> ");
	CommandExpect(session.app.label == "a\"b");
	CommandExpectText(Run(session, "mode slow \"a\\\\b\""), "mode slow \"a\\\\b\"\r\n> ");
	CommandExpect(session.app.label == "a\\b");

	// A quote that is not first is an ordinary character, and so is a backslash outside quotes.
	CommandExpectText(Run(session, "mode slow a\"b"), "mode slow a\"b\r\n> ");
	CommandExpect(session.app.label == "a\"b");

	// An unquoted text ends at a space.
	CommandExpectText(Run(session, "mode slow ab"), "mode slow ab\r\n> ");
	CommandExpect(session.app.label == "ab");

	// A quote left open at Enter.
	CommandExpectText(Run(session, "mode slow \"ab"), "mode slow \"ab\r\nmode: missing closing quote\r\n> ");

	// Too long: the text as typed counts its quotes, so eight characters fit and the ninth is refused.
	CommandExpectText(Run(session, "mode slow \"abcdefghi\""), "mode slow \"abcdefghi\"\r\nmode: label must be at most 8 characters\r\n> ");
	CommandExpectText(Run(session, "mode slow \"abcdef\""), "mode slow \"abcdef\"\r\n> ");
	CommandExpect(session.app.label == "abcdef");

	// Text after the closing quote.
	CommandExpectText(Run(session, "mode slow \"ab\"x"), "mode slow \"ab\"x\r\nmode: label is not valid\r\n> ");

	// A space inside quotes is part of the text. After the closing quote, a space ends the argument.
	CommandExpectText(Run(session, "mode slow \"a b\" c"), "mode slow \"a b\" c\r\nmode: too many arguments\r\n> ");
}


void TestInterrupt() {
	// With OnInterrupt, the handler runs when the byte is fed, before Process. Process cancels the line.
	Session<PlainInterrupt> withHandler;
	withHandler.Type("key 1");
	CommandExpectText(withHandler.Take(), "key 1");
	withHandler.console.Feed(0x03);
	CommandExpect(withHandler.app.interrupts == 1);
	CommandExpectText(withHandler.Take(), "");
	withHandler.console.Process();
	CommandExpectText(withHandler.Take(), "^C\r\n> ");
	CommandExpect(withHandler.app.keyCode == -1);

	// The line typed after the cancel is a new line.
	CommandExpectText(Run(withHandler, "key 2"), "key 2\r\n> ");
	CommandExpect(withHandler.app.keyCode == 2);

	// Ctrl+C with bytes already queued: they are echoed, then the line is cancelled, and nothing runs.
	Session<PlainInterrupt> queued;
	queued.console.Feed('k');
	queued.console.Feed('e');
	queued.console.Feed('y');
	queued.console.Feed(' ');
	queued.console.Feed('7');
	queued.console.Feed(0x03);
	CommandExpect(queued.app.interrupts == 1);
	queued.console.Process();
	CommandExpectText(queued.Take(), "key 7^C\r\n> ");
	CommandExpect(queued.app.keyCode == -1);

	// Without OnInterrupt, the Interrupt event is the only effect: the line is cancelled and the prompt returns.
	Session<PlainCommands> plain;
	plain.Type("key 1");
	plain.Take();
	plain.console.Feed(0x03);
	CommandExpect(plain.app.interrupts == 0);
	CommandExpectText(plain.Take(), "");
	plain.console.Process();
	CommandExpectText(plain.Take(), "^C\r\n> ");
	CommandExpect(plain.app.keyCode == -1);

	// Ctrl+C at an empty prompt cancels nothing. The Default() gets the event.
	Session<TraceCommands> traced;
	traced.console.Feed(0x03);
	traced.console.Process();
	CommandExpectText(traced.Take(), "> ");
	CommandExpect(traced.app.interrupts == 1);
	CommandExpectText(traced.app.log, Ev("Interrupt"));
}


void TestTerminal() {
	// The greeting and the prompt come with the open. The close prints nothing for the standard handler.
	Session<PlainCommands> session;
	session.console.SetTerminalOpen(true);
	session.console.Process();
	CommandExpectText(session.Take(), "VHAL command line\r\n> ");

	session.console.SetTerminalOpen(false);
	session.console.Process();
	CommandExpectText(session.Take(), "");

	// A line half typed is dropped when the terminal opens again, so the next line starts clean.
	session.console.SetTerminalOpen(true);
	session.Type("ke");
	session.Take();
	session.console.SetTerminalOpen(false);
	session.console.Process();
	session.Take();
	session.console.SetTerminalOpen(true);
	session.console.Process();
	CommandExpectText(session.Take(), "VHAL command line\r\n> ");
	CommandExpectText(Run(session, "key 4"), "key 4\r\n> ");
	CommandExpect(session.app.keyCode == 4);

	// The open and the close reach a Default(), and the prompt still comes after the open.
	Session<TraceCommands> traced;
	traced.console.SetTerminalOpen(true);
	traced.console.Process();
	CommandExpectText(traced.Take(), "> ");
	CommandExpectText(traced.app.log, Ev("TerminalOpened"));
	traced.console.SetTerminalOpen(false);
	traced.console.Process();
	CommandExpectText(traced.app.log, Ev("TerminalOpened") + Ev("TerminalClosed"));
}


void TestInputOverflow() {
	// The ring holds 32 bytes. The bytes that do not fit are lost, counted, and raised as InputOverflow before the rest is read.
	Session<PlainCommands> session;
	for (int index = 0; index < 40; index++) {
		session.console.Feed('a');
	}

	session.console.Process();
	CommandExpectText(session.Take(), "input lost\r\n" + std::string(32, 'a'));

	// The count is taken once: a second Process reports nothing more.
	session.console.Process();
	CommandExpectText(session.Take(), "");

	// A line whose end is lost is still a line: the next Enter ends it. Here the 33rd byte, the CR, does not fit.
	Session<PlainCommands> cut;
	std::string line = "key 1" + std::string(26, ' ') + "5\r";
	for (char character : line) {
		cut.console.Feed(static_cast<uint8>(character));
	}

	cut.console.Process();
	CommandExpectText(cut.Take(), "input lost\r\nkey 1" + std::string(26, ' ') + "5");
	cut.Type("\r");
	CommandExpectText(cut.Take(), "\r\nkey: too many arguments\r\n> ");
	CommandExpect(cut.app.keyCode == -1);
}


void TestWideRange() {
	// The range of a 64-bit unsigned argument is printed in full, and the values at its edges are read exactly.
	Session<WideCommands> session;
	CommandExpectText(Run(session, "take 18446744073709551616"), "take 18446744073709551616\r\ntake: v must be 0..18446744073709551615\r\n> ");
	CommandExpect(session.app.wideValue == 0);

	CommandExpectText(Run(session, "take 18446744073709551615"), "take 18446744073709551615\r\n> ");
	CommandExpect(session.app.wideValue == 18446744073709551615ULL);

	CommandExpectText(Run(session, "take 9223372036854775808"), "take 9223372036854775808\r\n> ");
	CommandExpect(session.app.wideValue == 9223372036854775808ULL);
}


void TestWordTruncation() {
	// A word longer than the word buffer is cut to 16 characters in its Word event, and the event says it was cut. A short word is
	// not marked.
	Session<CountingWords> session;
	CommandExpectText(Run(session, "abcdefghijklmnopqrstuvwxyz"), "abcdefghijklmnopqrstuvwxyz\r\n> ");
	CommandExpect(session.app.words == 1);
	CommandExpect(session.app.cutWords == 1);

	CommandExpectText(Run(session, "short"), "short\r\n> ");
	CommandExpect(session.app.words == 2);
	CommandExpect(session.app.cutWords == 1);
}


void TestRefusedInterrupt() {
	// The ring is full of "key 1" and spaces, so the Ctrl+C that follows is refused and counted as lost. The Interrupt still takes
	// place where the Ctrl+C came: the bytes before it are read and echoed, the line is cancelled, and the typed text does not run.
	Session<PlainCommands> session;
	std::string line = "key 1" + std::string(27, ' ');
	for (char character : line) {
		session.console.Feed(static_cast<uint8>(character));
	}

	session.console.Feed(0x03);
	session.console.Process();
	CommandExpectText(session.Take(), "input lost\r\nkey 1" + std::string(27, ' ') + "^C\r\n> ");
	CommandExpect(session.app.keyCode == -1);

	// The cancelled text is gone, so the next Enter ends an empty line and runs nothing.
	CommandExpectText(Run(session, ""), "\r\n> ");
	CommandExpect(session.app.keyCode == -1);

	// The same through a Default(), which gets the InputOverflow and Interrupt events in that order.
	Session<TraceCommands> traced;
	std::string tracedLine = "key 1" + std::string(27, ' ');
	for (char character : tracedLine) {
		traced.console.Feed(static_cast<uint8>(character));
	}

	traced.console.Feed(0x03);
	traced.console.Process();
	CommandExpect(traced.app.interrupts == 1);
	CommandExpect(traced.app.keyCode == -1);
	CommandExpectText(traced.app.log, Ev("InputOverflow") + Ev("Interrupt"));
}


void TestInputRingSize() {
	// InputRing<8> is listed among the console's types, after the application, as the design's shape needs: the ring is 8 bytes, so
	// the ninth byte fed before a Process is lost.
	TraceApp app;
	CommandSim::RecordingPrint out;
	CommandLine<PlainCommands, TraceApp, InputRing<8>> small(app, out);
	std::string typed = "key 1 2 3";

	for (char character : typed) {
		small.Feed(static_cast<uint8>(character));
	}

	small.Process();
	CommandExpectText(out.Text(), "input lost\r\nkey 1 2 ");
	out.Clear();

	// The Enter after the lost bytes runs the line that was kept, and the extra argument is refused as before.
	small.Feed('\r');
	small.Process();
	CommandExpectText(out.Text(), "\r\nkey: too many arguments\r\n> ");
	CommandExpect(app.keyCode == -1);
}


void TestSplitInput() {
	// Bytes fed in pieces, with Process between them, parse as one burst.
	Session<PlainCommands> whole;
	whole.Type("key 5\r");
	std::string wholeText = whole.Take();

	Session<PlainCommands> pieces;
	pieces.console.Feed('k');
	pieces.console.Feed('e');
	pieces.console.Process();
	pieces.console.Feed('y');
	pieces.console.Feed(' ');
	pieces.console.Feed('5');
	pieces.console.Feed('\r');
	pieces.console.Process();
	CommandExpectText(pieces.Take(), wholeText);
	CommandExpect(pieces.app.keyCode == 5);

	// A line end split across Process calls is still one line end.
	Session<PlainCommands> ends;
	ends.console.Feed('\r');
	ends.console.Process();
	ends.console.Feed('\n');
	ends.console.Process();
	CommandExpectText(ends.Take(), "\r\n> ");
}


void TestAnsi() {
	// Arrow keys, Home and Delete inject nothing: the line is the characters typed.
	Session<PlainCommands> session;
	std::string up = std::string("\x1b") + "[A";
	std::string down = std::string("\x1b") + "[B";
	std::string left = std::string("\x1b") + "[D";
	std::string home = std::string("\x1b") + "OH";
	std::string del = std::string("\x1b") + "[3~";
	CommandExpectText(Run(session, up + "key 5" + left + down + home + del), "key 5\r\n> ");
	CommandExpect(session.app.keyCode == 5);

	// A two-byte escape sequence is dropped.
	CommandExpectText(Run(session, "k" + std::string("\x1b") + "xey 6"), "key 6\r\n> ");
	CommandExpect(session.app.keyCode == 6);

	// A sequence broken by a control byte: the control byte is read as input, here the line end of an empty line.
	session.Type(std::string("\x1b") + "[");
	CommandExpectText(session.Take(), "");
	session.Type("\r");
	CommandExpectText(session.Take(), "\r\n> ");
}


void TestWordsStandard() {
	// The default-only build without a Default(): words print nothing, and each line ends with the prompt.
	Session<PlainWords> session;
	CommandExpectText(Run(session, "a b  c"), "a b  c\r\n> ");
	CommandExpectText(Run(session, ""), "\r\n> ");
	CommandExpectText(Run(session, "xyz 1 2 3"), "xyz 1 2 3\r\n> ");
}


void TestWordsEvents() {
	// A word is committed when the next word starts, and the last one at Enter. The Word events come before EndOfLine.
	Session<TraceWords> session;
	CommandExpectText(Run(session, "a  b"), "a  b\r\n> ");
	CommandExpectText(session.app.log, Ev("Word", "a") + Ev("Word", "b") + Ev("EndOfLine"));
	session.app.log.clear();

	// An empty line, or a line of spaces, is an EmptyLine with no word events.
	CommandExpectText(Run(session, "   "), "   \r\n> ");
	CommandExpectText(session.app.log, Ev("EmptyLine"));
	session.app.log.clear();

	// A word longer than the buffer is cut in its text: 16 characters.
	CommandExpectText(Run(session, "abcdefghijklmnopqrs"), "abcdefghijklmnopqrs\r\n> ");
	CommandExpectText(session.app.log, Ev("Word", "abcdefghijklmnop") + Ev("EndOfLine"));
}


void TestWordBackspace() {
	// While only spaces follow a word, Backspace returns into it: the space is erased, then the word's last character.
	Session<TraceWords> session;
	session.Type("a ");
	session.Take();
	session.Type("\b");
	CommandExpectText(session.Take(), "\b \b");
	session.Type("b\r");
	CommandExpectText(session.Take(), "b\r\n> ");
	CommandExpectText(session.app.log, Ev("Word", "ab") + Ev("EndOfLine"));
	session.app.log.clear();

	// Once the next word has started, the previous one is final. Erasing the current word stops at the space that keeps the two apart.
	Session<TraceWords> final;
	final.Type("a b");
	CommandExpectText(final.app.log, Ev("Word", "a"));
	final.Type("\b\b\b");
	CommandExpectText(Visible(final.Take()), "a ");
	final.Type("x\r");
	CommandExpectText(final.app.log, Ev("Word", "a") + Ev("Word", "x") + Ev("EndOfLine"));
}


// The Backspace property. Each line is typed with a junk string inserted at every position and erased again. The line that results must
// match the same line typed directly: the same messages, the same effects, and a visible line equal to the typed one.
void TestBackspaceEveryPosition() {
	const std::string lines[] = {
		"key 65",
		"key 0x41",
		"move -5 7 on",
		"move 1 2",
		"move 1 2 off",
		"mode slow \"a b\"",
		"mode fast ab",
		"fail",
		"motor speed -300",
		"motor pid gain 250",
		"motor stop",
		"a_b-9 1",
		"mode slow \"a\\\"b\"",
		"key",
		"key 1 2",
		"xyz",
		"motor",
		"mode medium",
		"move 1 2 maybe",
		"mode slow \"ab"
	};

	const std::string junks[] = {"x", "9", " ", "\"", "-", "0x", "z q", "\\", "1 2"};

	for (const std::string& line : lines) {
		Session<TraceCommands> direct;
		direct.Type(line + "\r");
		LineResult expected = SplitLine(direct.Take());
		std::string expectedSummary = Summary(direct.app);

		for (std::size_t position = 0; position <= line.size(); position++) {
			for (const std::string& junk : junks) {
				Session<TraceCommands> erased;
				erased.Type(line.substr(0, position));
				erased.Type(junk);
				erased.Type(std::string(junk.size(), '\b'));
				erased.Type(line.substr(position) + "\r");
				LineResult actual = SplitLine(erased.Take());

				std::string label = "line [" + line + "] junk [" + junk + "] at " + std::to_string(position);
				CommandSim::Expect(Visible(actual.echo) == line, label.c_str(), __FILE__, __LINE__);
				CommandSim::Expect(actual.messages == expected.messages, label.c_str(), __FILE__, __LINE__);
				CommandSim::Expect(Summary(erased.app) == expectedSummary, label.c_str(), __FILE__, __LINE__);
			}
		}
	}
}


// The same for the default-only build. A junk word that starts after a word is final in the design, so the junk here is a single
// character or a space, which Backspace can always take back.
void TestWordBackspaceEveryPosition() {
	const std::string lines[] = {"a b", "ab cd", "a  bc d", "  x", "one two three"};
	const std::string junks[] = {"x", " ", "qz"};

	for (const std::string& line : lines) {
		Session<TraceWords> direct;
		direct.Type(line + "\r");
		std::string expectedLog = direct.app.log;

		for (std::size_t position = 0; position <= line.size(); position++) {
			for (const std::string& junk : junks) {
				Session<TraceWords> erased;
				erased.Type(line.substr(0, position));
				erased.Type(junk);
				erased.Type(std::string(junk.size(), '\b'));
				erased.Type(line.substr(position) + "\r");

				std::string label = "words [" + line + "] junk [" + junk + "] at " + std::to_string(position);
				CommandSim::Expect(Visible(SplitLine(erased.Take()).echo) == line, label.c_str(), __FILE__, __LINE__);
				CommandSim::Expect(erased.app.log == expectedLog, label.c_str(), __FILE__, __LINE__);
			}
		}
	}
}


void TestEveryDeclaredEvent() {
	// Each event of the table, raised with its fields, and read by the Default() of the set.
	Session<TraceCommands> session;
	session.Type("xyz\r");
	CommandExpectText(session.app.log, Ev("UnknownCommand", "xyz"));
	session.app.log.clear();

	session.Type("fail\r");
	CommandExpectText(session.app.log, Ev("CommandFailed", "", "fail", "", 0, static_cast<int>(ResultStatus::invalidParameter)));
	session.app.log.clear();

	session.Type("key 300\r");
	CommandExpectText(session.app.log, Ev("ArgumentError", "", "key", "code", 2));
	session.app.log.clear();

	session.Type("key\r");
	CommandExpectText(session.app.log, Ev("ArgumentError", "", "key", "code", 4));
	session.app.log.clear();

	session.Type("key 1 2\r");
	CommandExpectText(session.app.log, Ev("ArgumentError", "", "key", "", 5));
	session.app.log.clear();

	session.Type("\r");
	CommandExpectText(session.app.log, Ev("EmptyLine"));
	session.app.log.clear();

	session.Type("key 1");
	session.Take();
	session.Type("\x03");
	CommandExpectText(session.app.log, Ev("Interrupt"));
	session.app.log.clear();

	// A set with no Default() gets the standard texts for the same events.
	Session<PlainCommands> plain;
	CommandExpectText(Run(plain, "key 300"), "key 300\r\nkey: code must be 0..255\r\n> ");
	CommandExpectText(Run(plain, "fail"), "fail\r\nfail: invalidParameter\r\n> ");
	CommandExpectText(Run(plain, "motor pid gain 1001"), "motor pid gain 1001\r\ngain: value must be -1000..1000\r\n> ");
}


}


int main() {
	TestEcho();
	TestLineEnds();
	TestRanges();
	TestCommandsAndMessages();
	TestStrings();
	TestInterrupt();
	TestTerminal();
	TestInputOverflow();
	TestWideRange();
	TestWordTruncation();
	TestRefusedInterrupt();
	TestInputRingSize();
	TestSplitInput();
	TestAnsi();
	TestWordsStandard();
	TestWordsEvents();
	TestWordBackspace();
	TestBackspaceEveryPosition();
	TestWordBackspaceEveryPosition();
	TestEveryDeclaredEvent();

	return CommandSim::Finish("TestCommandLine");
}
