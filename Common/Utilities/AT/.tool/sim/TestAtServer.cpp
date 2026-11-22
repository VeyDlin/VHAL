#include <VHAL.h>
#include <Utilities/AT/AtServer.h>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

// Host tests of the server: every form and argument type, chaining, the line endings, the dialect items with their numeric and verbose
// results, the CME modes, the URC queue, the ring's drop counter, and the same input fed whole or byte by byte. Each expected text is the
// design's framing (S3 S4 around an info line and a verbose result, a numeric code with S3 after it) written here by hand.


namespace {


int failureCount = 0;
int checkCount = 0;


// Shows CR and LF as text, so a failing expectation can be read.
std::string Visible(const std::string& text) {
	std::string shown;

	for (char character : text) {
		if (character == '\r') {
			shown += "\\r";
		} else if (character == '\n') {
			shown += "\\n";
		} else if (character == '"') {
			shown += "\\\"";
		} else {
			shown += character;
		}
	}

	return shown;
}


void ExpectText(const std::string& actual, const std::string& expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(
			stderr,
			"TestAtServer.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
			line,
			expression,
			Visible(actual).c_str(),
			Visible(expected).c_str()
		);
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtServer.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// The result code and the info line of the verbose form.
std::string Ok() {
	return "\r\nOK\r\n";
}


std::string Error() {
	return "\r\nERROR\r\n";
}


std::string Info(const std::string& text) {
	return "\r\n" + text + "\r\n";
}


// The application: the state the handlers read and change.
struct App {
	int16 temperature = 23;
	bool led = false;
	int8 mode = 0;
	std::string ssid;
	std::string password;
	bool hasPassword = false;
	uint32 sizeTotal = 0;
	int64 lastLong = 0;
	int basicValue = -1;
	int basicRuns = 0;
	int runs = 0;
};


Result<int16, AtStatus> Temp(AtCall<App> call) {
	return call.app.temperature;
}


AtStatus Led(AtCall<App> call, bool on) {
	call.app.led = on;
	return AtStatus{};
}


Result<int8, AtStatus> ModeRead(AtCall<App> call) {
	return call.app.mode;
}


AtStatus ModeWrite(AtCall<App> call, int8 mode) {
	call.app.mode = mode;
	return AtStatus{};
}


AtStatus Join(AtCall<App> call, std::string_view ssid, std::optional<std::string_view> password) {
	call.app.ssid = std::string(ssid);
	call.app.hasPassword = password.has_value();
	call.app.password = password ? std::string(*password) : std::string();
	return AtStatus{};
}


Result<uint32, AtStatus> SizesRead(AtCall<App> call) {
	return call.app.sizeTotal;
}


AtStatus SizesWrite(AtCall<App> call, uint8 a, uint16 b, uint32 c, int64 d) {
	call.app.sizeTotal = static_cast<uint32>(a) + b + c;
	call.app.lastLong = d;
	return AtStatus{};
}


Result<String<8>, AtStatus> NameRead(AtCall<App> call) {
	static_cast<void>(call);
	String<8> text;
	text.characters[0] = 'a';
	text.characters[1] = 'b';
	text.characters[2] = 'c';
	text.length = 3;
	return text;
}


AtStatus RunPlain(AtCall<App> call) {
	call.app.runs++;
	return AtStatus{};
}


AtStatus FailCme3(AtCall<App> call) {
	static_cast<void>(call);
	return AtCmeError(3);
}


AtStatus FailCme4(AtCall<App> call) {
	static_cast<void>(call);
	return AtCmeError(4);
}


Result<int16, AtStatus> Busy(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{ResultStatus::busy};
}


AtStatus BasicX(AtCall<App> call, uint8 on) {
	call.app.basicValue = on;
	return AtStatus{};
}


AtStatus BasicI(AtCall<App> call) {
	call.app.basicRuns++;
	return AtStatus{};
}


// The description with every form, every argument type and every dialect item. The dialect's CME text is for code 3 only.
using Full = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Dialect(AtDialect().Echo().Verbose().Quiet().Registers().CmeCommand().CmeText<3, "operation not allowed">())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password"))
		.Command<"+SIZES">().Test().Read<uint32>("total").Write(Argument<uint8>("a"), Argument<uint16>("b"), Argument<uint32>("c"), Argument<int64>("d"))
		.Command<"+NAME">().Read<String<8>>("name")
		.Command<"+RUN">().Run()
		.Command<"+FAIL">().Run()
		.Command<"+OTHER">().Run()
		.Command<"+BUSY">().Read<int16>("value")
		.Basic<"X">().Value(Argument<uint8>("on"))
		.Basic<"I">().Run()
		.Urc<"WIFI GOT IP">()
		.Urc<"+IPD">(Argument<uint16>("length"), Argument<String<8>>("tag"));
}>;


using FullBinding = AtServerBinding<
	AtOn<"+TEMP", &Temp>,
	AtOn<"+LED", &Led>,
	AtOn<"+CWMODE", &ModeRead>,
	AtOn<"+CWMODE", &ModeWrite>,
	AtOn<"+JOIN", &Join>,
	AtOn<"+SIZES", &SizesRead>,
	AtOn<"+SIZES", &SizesWrite>,
	AtOn<"+NAME", &NameRead>,
	AtOn<"+RUN", &RunPlain>,
	AtOn<"+FAIL", &FailCme3>,
	AtOn<"+OTHER", &FailCme4>,
	AtOn<"+BUSY", &Busy>,
	AtOn<"X", &BasicX>,
	AtOn<"I", &BasicI>
>;


// A description with no dialect item: no echo, verbose or CME code, and the default line framing.
using Bare = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Basic<"I">().Run();
}>;


using BareBinding = AtServerBinding<
	AtOn<"+TEMP", &Temp>,
	AtOn<"+LED", &Led>,
	AtOn<"I", &BasicI>
>;


// The output writer: the text the server writes, in the order it is written.
struct CapturePrint : public Print {
	std::string text;

	void WriteRaw(const char* data, size_t size) override {
		text.append(data, size);
	}
};


// A server with the set's own handlers, and the input it is fed. Each send returns the text the server wrote.
template<typename SetType, typename Binding, std::size_t Depth = 4, std::size_t Ring = 64>
struct Session {
	App app;
	CapturePrint out;
	AtServer<SetType, App, Binding, Depth, Ring> server{app, out};


	// Feeds the input. A full ring is run before the next byte, so no byte is dropped here. eachByte runs the server after every byte.
	std::string Send(std::string_view input, bool eachByte = false) {
		for (char character : input) {
			while (!server.Feed(static_cast<uint8>(character))) {
				server.Process();
			}

			if (eachByte) {
				server.Process();
			}
		}

		server.Process();
		std::string text = out.text;
		out.text.clear();
		return text;
	}
};


void TestForms() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	AT_TEXT(s.Send("AT\r"), Ok());
	AT_TEXT(s.Send("at+temp?\r"), Info("+TEMP: 23") + Ok());
	AT_TEXT(s.Send("AT+TEMP=1\r"), Error());
	AT_TEXT(s.Send("AT+TEMP\r"), Error());
	AT_TEXT(s.Send("AT+LED=1\r"), Ok());
	AT_EQ(s.app.led, 1);
	AT_TEXT(s.Send("AT+LED=0\r"), Ok());
	AT_EQ(s.app.led, 0);
	AT_TEXT(s.Send("AT+LED=2\r"), Error());
	AT_TEXT(s.Send("AT+LED=1,1\r"), Error());
	AT_TEXT(s.Send("AT+LED\r"), Error());
	AT_TEXT(s.Send("AT+NOPE?\r"), Error());
	AT_TEXT(s.Send("AT+CWMODE=?\r"), Info("+CWMODE: (-128-127)") + Ok());
	AT_TEXT(s.Send("AT+CWMODE=5\r"), Ok());
	AT_TEXT(s.Send("AT+CWMODE?\r"), Info("+CWMODE: 5") + Ok());
	AT_TEXT(s.Send("AT+CWMODE=128\r"), Error());
	AT_TEXT(s.Send("AT+CWMODE=-128\r"), Ok());
	AT_EQ(s.app.mode, -128);
	AT_TEXT(s.Send("AT+SIZES=?\r"),
		Info("+SIZES: (0-255),(0-65535),(0-4294967295),(-9223372036854775808-9223372036854775807)") + Ok());
	AT_TEXT(s.Send("AT+SIZES=1,2,3,-5\r"), Ok());
	AT_EQ(s.app.sizeTotal, 6);
	AT_EQ(s.app.lastLong, -5);
	AT_TEXT(s.Send("AT+SIZES=1,2,3,-9223372036854775808\r"), Ok());
	AT_EQ(s.app.lastLong, -9223372036854775807LL - 1);
	AT_TEXT(s.Send("AT+SIZES=256,2,3,4\r"), Error());
	AT_TEXT(s.Send("AT+SIZES?\r"), Info("+SIZES: 6") + Ok());
	AT_TEXT(s.Send("AT+NAME?\r"), Info("+NAME: \"abc\"") + Ok());
	AT_TEXT(s.Send("AT+RUN\r"), Ok());
	AT_EQ(s.app.runs, 1);
	AT_TEXT(s.Send("AT+BUSY?\r"), Error());
	AT_TEXT(s.Send("ATX1\r"), Ok());
	AT_EQ(s.app.basicValue, 1);
	AT_TEXT(s.Send("ATX\r"), Error());
	AT_TEXT(s.Send("ATX=1\r"), Error());
	AT_TEXT(s.Send("ATI\r"), Ok());
	AT_EQ(s.app.basicRuns, 1);
	AT_TEXT(s.Send("ATZ\r"), Error());
	AT_TEXT(s.Send("XYZ\r"), Error());
	AT_TEXT(s.Send("AT;\r"), Ok());
	AT_TEXT(s.Send("AT +TEMP?\r"), Error());
	AT_TEXT(s.Send("\r"), "");
}


void TestTypesAndStrings() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	AT_TEXT(s.Send("AT+JOIN=\"my,net\",\"pa\\\"ss\"\r"), Ok());
	AT_TEXT(s.app.ssid, "my,net");
	AT_TEXT(s.app.password, "pa\"ss");
	AT_EQ(s.app.hasPassword, 1);

	AT_TEXT(s.Send("AT+JOIN=\"net\"\r"), Ok());
	AT_TEXT(s.app.ssid, "net");
	AT_EQ(s.app.hasPassword, 0);

	AT_TEXT(s.Send("AT+JOIN=\"net\",\r"), Ok());
	AT_EQ(s.app.hasPassword, 0);

	AT_TEXT(s.Send("AT+JOIN=\"a\",\"b\",\"c\"\r"), Error());
	AT_TEXT(s.Send("AT+JOIN=\"unterminated\r"), Error());
	AT_TEXT(s.Send("AT+JOIN=\"a\"x\r"), Error());
	AT_TEXT(s.Send("AT+JOIN=\"%s\"\r"), Ok());
	AT_TEXT(s.Send("AT+JOIN=\"0123456789012345678901234567890123\"\r"), Error());
	AT_TEXT(s.Send("AT+JOIN=\"\"\r"), Ok());
	AT_TEXT(s.app.ssid, "");
}


void TestChaining() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	AT_TEXT(s.Send("AT+TEMP?;+TEMP?\r"), Info("+TEMP: 23") + Info("+TEMP: 23") + Ok());
	AT_TEXT(s.Send("AT+LED=1;+LED=0\r"), Ok());
	AT_EQ(s.app.led, 0);

	// The first error stops the rest of the line: the LED is set, and the temperature is not read.
	AT_TEXT(s.Send("AT+LED=1;+NOPE;+TEMP?\r"), Error());
	AT_EQ(s.app.led, 1);
	AT_TEXT(s.Send("AT+LED=0;+FAIL;+TEMP?\r"), Error());
	AT_EQ(s.app.led, 0);
	AT_TEXT(s.Send("AT+TEMP?;+LED=9;+TEMP?\r"), Info("+TEMP: 23") + Error());
	AT_TEXT(s.Send("ATX1;I;X0\r"), Ok());
	AT_EQ(s.app.basicValue, 0);
	AT_EQ(s.app.basicRuns, 1);
}


void TestLineEndings() {
	Session<Bare, BareBinding> s;

	AT_TEXT(s.Send("AT\r\n"), Ok());
	AT_TEXT(s.Send("AT\n"), Ok());
	AT_TEXT(s.Send("AT\n\r"), Ok());
	AT_TEXT(s.Send("AT\r\r"), Ok());
	AT_TEXT(s.Send("AT\r\nAT\r\n"), Ok() + Ok());
	AT_TEXT(s.Send("AT\n\nAT\r"), Ok() + Ok());
	AT_TEXT(s.Send("AT+TEMP?\r\n"), Info("+TEMP: 23") + Ok());
	AT_TEXT(s.Send("AT\r\n\r\n"), Ok());
	AT_TEXT(s.Send("ATE0\r"), Error());
}


void TestDialect() {
	Session<Full, FullBinding> s;

	// ATE: the echo follows the state at each byte, and ATE0 is echoed before it takes effect.
	AT_TEXT(s.Send("AT\r"), "AT\r" + Ok());
	AT_TEXT(s.Send("ATE0\r"), "ATE0\r" + Ok());
	AT_TEXT(s.Send("AT\r"), Ok());
	AT_TEXT(s.Send("ATE1\r"), Ok());
	AT_TEXT(s.Send("ATE2\r"), "ATE2\r" + Error());

	// ATV0: numeric result codes, OK=0 and ERROR=4, with S3 after the code and no info framing on the result.
	AT_TEXT(s.Send("ATV0\r"), "ATV0\r0\r");
	AT_TEXT(s.Send("AT\r"), "AT\r0\r");
	AT_TEXT(s.Send("AT+LED=2\r"), "AT+LED=2\r4\r");
	AT_TEXT(s.Send("ATV1\r"), "ATV1\r" + Ok());

	// ATQ1: no result code, but the info lines are still sent.
	AT_TEXT(s.Send("ATQ1\r"), "ATQ1\r");
	AT_TEXT(s.Send("AT+TEMP?\r"), "AT+TEMP?\r" + Info("+TEMP: 23"));
	AT_TEXT(s.Send("AT+LED=2\r"), "AT+LED=2\r");
	AT_TEXT(s.Send("ATQ0\r"), "ATQ0\r" + Ok());

	// ATS3 and ATS4: read as three digits, and changed at run time. The new terminator is used for the reply after the change.
	s.Send("ATE0\r");
	AT_TEXT(s.Send("ATS3?\r"), Info("013") + Ok());
	AT_TEXT(s.Send("ATS4?\r"), Info("010") + Ok());
	// While S3 is 10 a line ends at LF, and CR is an ordinary character. So the commands sent after ATS3=10 end with LF.
	AT_TEXT(s.Send("ATS3=10\r"), "\n\nOK\n\n");
	AT_TEXT(s.Send("AT\n"), "\n\nOK\n\n");
	AT_TEXT(s.Send("ATS3?\n"), "\n\n010\n\n\n\nOK\n\n");
	AT_TEXT(s.Send("ATS3=13\n"), Ok());
	AT_TEXT(s.Send("ATS5?\r"), Error());
	// V.250 gives S3 and S4 the range 0 to 127, so 127 is the largest value and 128 is an error. The reply to ATS3=127 uses DEL as its
	// terminator, so the command after it ends with DEL, and S3 is put back to 13 by that line.
	AT_TEXT(s.Send("ATS3=127\r"), "\x7f\nOK\x7f\n");
	AT_TEXT(s.Send("ATS3=13\x7f"), Ok());
	AT_TEXT(s.Send("ATS3=128\r"), Error());
	AT_TEXT(s.Send("ATS3=256\r"), Error());
	AT_TEXT(s.Send("ATS3\r"), Error());
}


void TestCme() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	// CMEE 0 is the default: a CME error is ERROR.
	AT_TEXT(s.Send("AT+FAIL\r"), Error());
	AT_TEXT(s.Send("AT+CMEE?\r"), Info("+CMEE: 0") + Ok());
	AT_TEXT(s.Send("AT+CMEE=1\r"), Ok());
	AT_TEXT(s.Send("AT+FAIL\r"), Info("+CME ERROR: 3") + "");
	AT_TEXT(s.Send("AT+CMEE=2\r"), Ok());
	AT_TEXT(s.Send("AT+FAIL\r"), Info("+CME ERROR: operation not allowed") + "");
	AT_TEXT(s.Send("AT+OTHER\r"), Info("+CME ERROR: 4") + "");
	AT_TEXT(s.Send("AT+CMEE=?\r"), Info("+CMEE: (0-2)") + Ok());
	AT_TEXT(s.Send("AT+CMEE=3\r"), Error());
	AT_TEXT(s.Send("AT+CMEE=0\r"), Ok());
	AT_TEXT(s.Send("AT+FAIL\r"), Error());
	AT_TEXT(s.Send("AT+BUSY?\r"), Error());
}


// URCs: a queued URC is written after the final line of the command that is running, and never inside a reply.
void TestUrcOrder() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	AT_TEXT(s.Send("AT+TEMP?;"), Info("+TEMP: 23"));
	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{}, 1);
	AT_EQ(s.server.Urc<"+IPD">(static_cast<uint16>(5), String<8>{{'a', 'b'}, 2}) == AtStatus{}, 1);
	AT_TEXT(s.Send(""), "");
	AT_TEXT(s.Send("\r"), Ok() + "\r\nWIFI GOT IP\r\n" + "\r\n+IPD,5,\"ab\"\r\n");

	// Queued while idle, the URCs are written by the next Process.
	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{}, 1);
	AT_TEXT(s.Send(""), "\r\nWIFI GOT IP\r\n");

	// A handler's reply is complete before the URC, even when the URC is queued during the line.
	AT_EQ(s.server.Urc<"+IPD">(static_cast<uint16>(1), String<8>{}) == AtStatus{}, 1);
	AT_TEXT(s.Send("AT+TEMP?\r"), Info("+TEMP: 23") + Ok() + "\r\n+IPD,1,\"\"\r\n");
}


void TestUrcQueueFull() {
	Session<Full, FullBinding, 2> s;

	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{}, 1);
	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{}, 1);
	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{ResultStatus::filled}, 1);
	AT_TEXT(s.Send(""), "\r\nWIFI GOT IP\r\n\r\nWIFI GOT IP\r\n");
	AT_EQ(s.server.Urc<"WIFI GOT IP">() == AtStatus{}, 1);
	AT_TEXT(s.Send(""), "\r\nWIFI GOT IP\r\n");
}


// The ring: a full ring drops the byte and counts it. The bytes it takes are run in order.
void TestRingDrops() {
	Session<Bare, BareBinding, 4, 64> s;
	int refused = 0;

	for (int index = 0; index < 70; index++) {
		if (!s.server.Feed(static_cast<uint8>('x'))) {
			refused++;
		}
	}

	AT_EQ(refused, 6);
	AT_EQ(s.server.DroppedBytes(), 6);
	AT_EQ(s.server.DroppedBytes(), 6);
	s.server.Process();
	AT_TEXT(s.Send("\r"), Error());
	AT_TEXT(s.Send("AT\r"), Ok());
}


// The same input, fed whole or byte by byte, gives the same text.
void TestChunking() {
	const std::string script = "AT+TEMP?\rAT+LED=1\r\nAT+CWMODE=?\r+SIZES;AT+SIZES=1,2,3,4\rATE0\rAT+NAME?\rAT+JOIN=\"a,b\",\"c\"\r";
	Session<Full, FullBinding> whole;
	Session<Full, FullBinding> single;

	std::string wholeText = whole.Send(script);
	std::string singleText = single.Send(script, true);
	AT_TEXT(singleText, wholeText);
	AT_TEXT(wholeText.substr(0, 9), "AT+TEMP?\r");
}


// V.250 concatenated basic commands: a letter ends a basic command's value, so ATV0V0 is ATV0 and then ATV0, and ATEV1 is ATE with no
// value (off, as V.250 gives) and then ATV1. Echo is off from the first line on, so only the result codes are checked.
void TestConcatenatedBasic() {
	Session<Full, FullBinding> s;

	AT_TEXT(s.Send("ATE0\r"), "ATE0\r" + Ok());
	AT_TEXT(s.Send("ATV0V0\r"), "0\r");
	AT_TEXT(s.Send("AT\r"), "0\r");
	AT_TEXT(s.Send("ATV0V1\r"), Ok());
	AT_TEXT(s.Send("ATEV1\r"), Ok());
	AT_TEXT(s.Send("ATV0V1E0\r"), Ok());
	AT_TEXT(s.Send("ATV0+CMEE?\r"), Info("+CMEE: 0") + "0\r");
	AT_TEXT(s.Send("ATV1\r"), Ok());
}


// The dialect items the set does not declare: no echo, no verbose, no CME codes. The default line framing applies.
void TestBareDialect() {
	Session<Bare, BareBinding> s;

	AT_TEXT(s.Send("AT+TEMP?\r"), Info("+TEMP: 23") + Ok());
	AT_TEXT(s.Send("AT+LED=2\r"), Error());
	AT_TEXT(s.Send("ATE0\r"), Error());
	AT_TEXT(s.Send("ATV0\r"), Error());
	AT_TEXT(s.Send("AT+CMEE?\r"), Error());
	AT_TEXT(s.Send("ATS3?\r"), Error());
	AT_TEXT(s.Send("ATQ1\r"), Error());
}


// With the registers, a line ends at S3. After ATS3=35 a line ends at '#', and CR and LF are ordinary characters. Back at S3=13, LF ends a
// line too, and CR LF or LF CR is one end.
void TestInputTerminator() {
	Session<Full, FullBinding> s;
	s.Send("ATE0\r");

	AT_TEXT(s.Send("ATS3=35\r"), "#\nOK#\n");
	AT_TEXT(s.Send("AT+TEMP?#"), "#\n+TEMP: 23#\n#\nOK#\n");
	AT_TEXT(s.Send("AT#"), "#\nOK#\n");

	// CR does not end the line, and it is dropped, so the line is answered at the '#'.
	AT_TEXT(s.Send("AT+TEMP?\r"), "");
	AT_TEXT(s.Send("#"), "#\n+TEMP: 23#\n#\nOK#\n");

	// LF does not end the line either, and it is dropped too.
	AT_TEXT(s.Send("AT\n"), "");
	AT_TEXT(s.Send("#"), "#\nOK#\n");

	// A host that keeps sending CR LF: the CR LF are dropped, and each command ends at the '#'.
	AT_TEXT(s.Send("\r\nAT#\r\n"), "#\nOK#\n");
	AT_TEXT(s.Send("AT+TE\r\nMP?#"), "#\n+TEMP: 23#\n#\nOK#\n");

	AT_TEXT(s.Send("ATS3?#"), "#\n035#\n#\nOK#\n");
	AT_TEXT(s.Send("ATS3=13#"), Ok());

	AT_TEXT(s.Send("AT\n"), Ok());
	AT_TEXT(s.Send("AT\n\r"), Ok());
	AT_TEXT(s.Send("AT\r\n"), Ok());
	AT_TEXT(s.Send("AT+TEMP?\r\n"), Info("+TEMP: 23") + Ok());
	AT_TEXT(s.Send("ATS3=35\r"), "#\nOK#\n");
	AT_TEXT(s.Send("AT;+TEMP?#"), "#\n+TEMP: 23#\n#\nOK#\n");
	AT_TEXT(s.Send("ATS3=13#"), Ok());

	// The LF of a CR LF that changed S3 is dropped, so the next command is read.
	AT_TEXT(s.Send("ATS3=35\r\n"), "#\nOK#\n");
	AT_TEXT(s.Send("AT#"), "#\nOK#\n");

	// With S3 = 10, the CR of a CR LF host is dropped.
	AT_TEXT(s.Send("ATS3=10#"), "\n\nOK\n\n");
	AT_TEXT(s.Send("AT\r\n"), "\n\nOK\n\n");
	AT_TEXT(s.Send("ATS3=13\r\n"), Ok());
}


// A set with CmeErrors and CmeCommand: the CME mode starts at the mode of CmeErrors, and AT+CMEE changes it at run time.
using CmeBoth = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().CmeErrors(AtCmeMode::numeric).CmeCommand())
		.Command<"+FAIL">().Run();
}>;


using CmeBothBinding = AtServerBinding<AtOn<"+FAIL", &FailCme3>>;


// A set with CmeErrors alone: the mode is fixed, and the set has no AT+CMEE.
using CmeFixed = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().CmeErrors(AtCmeMode::numeric))
		.Command<"+FAIL">().Run();
}>;


using CmeFixedBinding = AtServerBinding<AtOn<"+FAIL", &FailCme3>>;


// The two orders of the CME items give the same set.
using CmeReversed = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().CmeCommand().CmeErrors(AtCmeMode::verbose))
		.Command<"+FAIL">().Run();
}>;


void TestCmeCommandAndErrors() {
	Session<CmeBoth, CmeBothBinding> s;

	static_assert(CmeBoth::CmeMode == AtCmeMode::numeric);
	static_assert(CmeReversed::CmeMode == AtCmeMode::verbose);
	static_assert(CmeFixed::Has<AtCmeCommandItem> == false);

	// The mode at start is the CmeErrors mode, so AT+CMEE? reports it, and an error uses it.
	AT_TEXT(s.Send("AT+CMEE?\r"), Info("+CMEE: 1") + Ok());
	AT_TEXT(s.Send("AT+FAIL\r"), Info("+CME ERROR: 3"));

	// AT+CMEE=0 changes the mode, and a later error is a plain ERROR.
	AT_TEXT(s.Send("AT+CMEE=0\r"), Ok());
	AT_TEXT(s.Send("AT+CMEE?\r"), Info("+CMEE: 0") + Ok());
	AT_TEXT(s.Send("AT+FAIL\r"), Error());

	// CmeErrors alone is fixed: the set has no AT+CMEE, so the name is unknown and gives ERROR.
	Session<CmeFixed, CmeFixedBinding> fixed;
	AT_TEXT(fixed.Send("AT+FAIL\r"), Info("+CME ERROR: 3"));
	AT_TEXT(fixed.Send("AT+CMEE?\r"), Error());
}


void TestStaticTypes() {
	static_assert(std::is_same_v<Full::CommandOf<"+CWMODE">::ReadType, int8>);
	static_assert(std::is_same_v<Full::CommandOf<"+SIZES">::ReadType, uint32>);
	static_assert(std::is_same_v<Full::CommandOf<"+LED">::WriteArguments, std::tuple<AtArgument<bool, false>>>);
	static_assert(Full::CmeMode == AtCmeMode::off);
	static_assert(Full::BuiltinCount == 5);
	static_assert(Bare::BuiltinCount == 0);
}


} // namespace


int main() {
	TestStaticTypes();
	TestForms();
	TestTypesAndStrings();
	TestChaining();
	TestLineEndings();
	TestDialect();
	TestConcatenatedBasic();
	TestCme();
	TestCmeCommandAndErrors();
	TestInputTerminator();
	TestUrcOrder();
	TestUrcQueueFull();
	TestRingDrops();
	TestChunking();
	TestBareDialect();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtServer: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestAtServer: all %d checks passed\n", checkCount);
	return 0;
}
