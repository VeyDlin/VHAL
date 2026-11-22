#include <VHAL.h>
#include <Utilities/AT/AtServer.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandSet.h>
#include <cstdio>
#include <string>
#include <string_view>

// A translation unit with both the command line and the AT library. Each one declares its own set with Argument<T>, and the two sets use the
// same class. The AT set has no Argument function of its own, so the names do not clash. Each set runs its own command, and the range of
// the same declaration is checked the same way on both sides.


namespace {


int failureCount = 0;
int checkCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtConsoleArguments.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectText(const std::string& actual, const std::string& expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtConsoleArguments.cpp:%d: %s is \"%s\", expected \"%s\"\n", line, expression, actual.c_str(), expected.c_str());
	}
}


#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)


// The output writer of both sides. The text is kept, so the tests can read what was written.
struct CapturePrint : public Print {
	std::string text;

	void WriteRaw(const char* data, size_t size) override {
		text.append(data, size);
	}
};


// The command line's application: the value its command stores.
struct ConsoleApp {
	int code = -1;
};


ResultStatus ConsoleKey(CommandCall<ConsoleApp> call, uint8 code) {
	call.app.code = code;
	return ResultStatus::ok;
}


// The console's set: one command whose argument is Argument<uint8> with a range, the same declaration the AT set uses.
using ConsoleSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ConsoleKey, Argument<uint8>("code").Range(2, 9));
}>;


// The AT application: the value its write form stores.
struct AtApp {
	int level = -1;
};


AtStatus AtLevel(AtCall<AtApp> call, uint8 level) {
	call.app.level = level;
	return AtStatus{};
}


// The AT set: the same argument declaration, with a range, in a write form.
using AtSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+LEVEL">().Test().Write(Argument<uint8>("level").Range(2, 9));
}>;


using AtBinding = AtServerBinding<AtOn<"+LEVEL", &AtLevel>>;


// The two sides read the same range from the same declaration.
static_assert(AtSet::fieldRanges[0].minimum == 2);
static_assert(AtSet::fieldRanges[0].maximum == 9);


// Runs the console over the input, one byte at a time, as a main loop does.
void ConsoleSend(CommandLine<ConsoleSet, ConsoleApp>& console, std::string_view input) {
	for (char character : input) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


// Runs the AT server over the input, and returns the text it wrote.
std::string AtSend(AtServer<AtSet, AtApp, AtBinding>& server, CapturePrint& out, std::string_view input) {
	for (char character : input) {
		while (!server.Feed(static_cast<uint8>(character))) {
			server.Process();
		}
	}

	server.Process();
	std::string text = out.text;
	out.text.clear();
	return text;
}


void TestConsoleSide() {
	ConsoleApp app;
	CapturePrint out;
	CommandLine<ConsoleSet, ConsoleApp> console(app, out);

	AT_EQ(ConsoleSet::ArgumentInfoOf(0, 0).minimum, 2);
	AT_EQ(ConsoleSet::ArgumentInfoOf(0, 0).maximum, 9);

	ConsoleSend(console, "key 2\r");
	AT_EQ(app.code, 2);
	ConsoleSend(console, "key 9\r");
	AT_EQ(app.code, 9);

	// One past each edge is refused by the console, so the handler keeps the last value it stored.
	ConsoleSend(console, "key 1\r");
	AT_EQ(app.code, 9);
	ConsoleSend(console, "key 10\r");
	AT_EQ(app.code, 9);
}


void TestAtSide() {
	AtApp app;
	CapturePrint out;
	AtServer<AtSet, AtApp, AtBinding> server(app, out);

	AT_TEXT(AtSend(server, out, "AT+LEVEL=2\r"), "\r\nOK\r\n");
	AT_EQ(app.level, 2);
	AT_TEXT(AtSend(server, out, "AT+LEVEL=9\r"), "\r\nOK\r\n");
	AT_EQ(app.level, 9);
	AT_TEXT(AtSend(server, out, "AT+LEVEL=1\r"), "\r\nERROR\r\n");
	AT_TEXT(AtSend(server, out, "AT+LEVEL=10\r"), "\r\nERROR\r\n");
	AT_EQ(app.level, 9);
	AT_TEXT(AtSend(server, out, "AT+LEVEL=?\r"), "\r\n+LEVEL: (2-9)\r\n\r\nOK\r\n");
}


} // namespace


int main() {
	TestConsoleSide();
	TestAtSide();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtConsoleArguments: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestAtConsoleArguments: all %d checks passed\n", checkCount);
	return 0;
}
