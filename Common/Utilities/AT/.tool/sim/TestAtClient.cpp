#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

// Host tests of the client: the typed calls and their encoding, the info lines and the value grammar, the finals and their statuses,
// CME codes, the URCs with their arguments, the echo module, the queue, the drain, and the timeouts at the deadline and across the 32-bit
// wrap. Each expected text is written here by hand from the design: the request is the design's AT line, a reply is the design's info
// line and final, and a status is the design's table.


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
			"TestAtClient.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
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
		std::fprintf(stderr, "TestAtClient.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// The log of the handlers, one line each. Each test clears it and reads it back.
std::string handled;


void Note(const std::string& text) {
	handled += text;
	handled += "\n";
}


// The status as the log names it: a CME code first, then the status name.
std::string Describe(AtStatus status) {
	if (status.cmeCode != 0) {
		return "error cme " + std::to_string(status.cmeCode);
	}

	switch (status.status) {
		case ResultStatus::ok:
			return "ok";
		case ResultStatus::error:
			return "error";
		case ResultStatus::timeout:
			return "timeout";
		case ResultStatus::busy:
			return "busy";
		case ResultStatus::invalidData:
			return "invalidData";
		case ResultStatus::invalidParameter:
			return "invalidParameter";
		case ResultStatus::filled:
			return "filled";
		default:
			return "other";
	}
}


// The output of the client, in the order it is written.
struct CapturePrint : public Print {
	std::string text;

	void WriteRaw(const char* data, size_t size) override {
		text.append(data, size);
	}
};


// The description: a dialect with a CME text, the forms, a reply prefix, finals and failures, a basic command and URCs.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().CmeCommand().CmeText<3, "operation not allowed">())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(300)
		.Command<"+SIZES">().Read<uint32>("total").Write(Argument<uint8>("a"), Argument<uint16>("b"), Argument<uint32>("c"), Argument<int64>("d"))
		.Command<"+NAME">().Read<String<8>>("name")
		.Command<"+CIFSR">().Read<String<16>>("ip").Reply("+CIFSR:STAIP")
		.Command<"+RUN">().Run().Finals("SEND OK").Failures("FAIL")
		.Basic<"E">().Value(Argument<uint8>("on"))
		.Urc<"WIFI GOT IP">()
		.Urc<"+IPD">(Argument<uint16>("length"), Argument<String<8>>("tag"), Optional<bool>("flag"))
		.Urc<"BELL">();
}>;


void OnTemp(Result<int16, AtStatus> result) {
	if (result.IsOk()) {
		Note("temp " + std::to_string(result.Value()));
	} else {
		Note("temp " + Describe(result.Error()));
	}
}


void OnMode(Result<int8, AtStatus> result) {
	if (result.IsOk()) {
		Note("mode " + std::to_string(result.Value()));
	} else {
		Note("mode " + Describe(result.Error()));
	}
}


void OnSizes(Result<uint32, AtStatus> result) {
	if (result.IsOk()) {
		Note("sizes read " + std::to_string(result.Value()));
	} else {
		Note("sizes read " + Describe(result.Error()));
	}
}


void OnName(Result<String<8>, AtStatus> result) {
	if (result.IsOk()) {
		Note("name " + std::string(result.Value().View()));
	} else {
		Note("name " + Describe(result.Error()));
	}
}


void OnIp(Result<String<16>, AtStatus> result) {
	if (result.IsOk()) {
		Note("ip " + std::string(result.Value().View()));
	} else {
		Note("ip " + Describe(result.Error()));
	}
}


void OnLed(AtStatus status) {
	Note("led " + Describe(status));
}


void OnJoin(AtStatus status) {
	Note("join " + Describe(status));
}


void OnCwmode(AtStatus status) {
	Note("cwmode " + Describe(status));
}


void OnSizesWrite(AtStatus status) {
	Note("sizes write " + Describe(status));
}


void OnRun(AtStatus status) {
	Note("run " + Describe(status));
}


void OnTest(AtStatus status) {
	Note("test " + Describe(status));
}


void OnValue(AtStatus status) {
	Note("value " + Describe(status));
}


void OnGotIp() {
	Note("got ip");
}


void OnIpd(uint16 length, std::string_view tag, std::optional<bool> flag) {
	std::string text = "ipd " + std::to_string(length) + " " + std::string(tag) + " ";

	if (flag.has_value()) {
		text += *flag ? "true" : "false";
	} else {
		text += "absent";
	}

	Note(text);
}


using Binding = AtClientBinding<
	AtUrcOn<"WIFI GOT IP", &OnGotIp>,
	AtUrcOn<"+IPD", &OnIpd>
>;


using Client = AtClient<Set, Binding>;
using EchoClient = AtClient<Set, Binding, AtQueue<2>, AtEcho>;


// Feeds the bytes of the text to the client, as a receive interrupt would. The ring holds 64 bytes, so the main loop runs after each
// chunk of 32, at the time nowMs. A test that counts dropped bytes feeds them itself.
template<typename ClientType>
void Feed(ClientType& client, std::string_view bytes, uint32 nowMs = 0) {
	std::size_t fed = 0;

	for (char character : bytes) {
		client.Feed(static_cast<uint8>(character));
		fed++;

		if (fed % 32 == 0) {
			client.Process(nowMs);
		}
	}

	client.Process(nowMs);
}


void Reset() {
	handled.clear();
	simCriticalEnters = 0;
}


// The integer and string reads: a signed 8-bit mode, an unsigned 32-bit total, and the info lines of each.
void TestReadIntegerTypes() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+CWMODE", &OnMode>();
	client.Process(0);
	AT_TEXT(out.text, "AT+CWMODE?\r\n");
	Feed(client, "+CWMODE: -1\r\nOK\r\n");
	client.Process(0);

	client.Read<"+SIZES", &OnSizes>();
	client.Process(0);
	Feed(client, "+SIZES: 4294967295\r\nOK\r\n");
	client.Process(0);

	client.Read<"+SIZES", &OnSizes>();
	client.Process(0);
	Feed(client, "+SIZES: -1\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "mode -1\nsizes read 4294967295\nsizes read invalidData\n");
}


// A read of +TEMP, sent at time zero. Its info line and final line are fed by the test.
void TestReadOk() {
	Reset();
	CapturePrint out;
	Client client(out);

	AtStatus status = client.Read<"+TEMP", &OnTemp>();
	AT_EQ(status.status == ResultStatus::ok, true);
	client.Process(0);
	AT_TEXT(out.text, "AT+TEMP?\r\n");

	Feed(client, "\r\n+TEMP: 23\r\n\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp 23\n");
	AT_EQ(simCriticalDepth, 0);
}


void TestReadLinesThatDoNotParse() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: x\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");

	Reset();
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: 40000\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");

	Reset();
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP:23\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp 23\n");

	Reset();
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP:  23\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");
}


// A single reply has one value line, so a second one makes it invalid rather than picking one of the two.
void TestSecondValueIsInvalid() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: 1\r\n+TEMP: 2\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");
}


void TestUnknownAndEmptyLinesIgnored() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "hello there\r\n\r\n\n+CWMODE: 3\r\n+TEMP: 2\r\n\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp 2\n");
}


void TestInfoLineTooLongIsInvalid() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	std::string longLine = "+TEMP: " + std::string(100, '1') + "\r\nOK\r\n";
	Feed(client, longLine);
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");

	Reset();
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "garbage " + std::string(300, 'g') + "\r\n+TEMP: 4\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp 4\n");
}


void TestCmeAndErrorFinals() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+CME ERROR: 3\r\n");
	client.Process(0);
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+CME ERROR: operation not allowed\r\n");
	client.Process(0);
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "ERROR\r\n");
	client.Process(0);
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+CME ERROR: 515\r\n");
	client.Process(0);
	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+CME ERROR: some other text\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp error cme 3\ntemp error cme 3\ntemp error\ntemp error cme 515\ntemp error\n");
}


void TestFinalWithoutInfoIsInvalidData() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");
}


void TestFailFinalAndDeclaredFinal() {
	Reset();
	CapturePrint out;
	Client client(out);

	AtStatus status = client.Run<"+RUN", &OnRun>();
	AT_EQ(status.status == ResultStatus::ok, true);
	client.Process(0);
	AT_TEXT(out.text, "AT+RUN\r\n");
	Feed(client, "SEND OK\r\n");
	client.Process(0);

	client.Run<"+RUN", &OnRun>();
	client.Process(0);
	Feed(client, "FAIL\r\n");
	client.Process(0);
	AT_TEXT(handled, "run ok\nrun error\n");
}


void TestWriteBool() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Write<"+LED", &OnLed>(true);
	client.Process(0);
	AT_TEXT(out.text, "AT+LED=1\r\n");
	Feed(client, "OK\r\n");
	client.Process(0);

	client.Write<"+LED", &OnLed>(false);
	client.Process(0);
	Feed(client, "ERROR\r\n");
	client.Process(0);
	AT_TEXT(out.text, "AT+LED=1\r\nAT+LED=0\r\n");
	AT_TEXT(handled, "led ok\nled error\n");
}


void TestWriteIntegersAndSigned() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Write<"+CWMODE", &OnCwmode>(int8{2});
	client.Process(0);
	Feed(client, "OK\r\n");
	client.Process(0);
	client.Write<"+SIZES", &OnSizesWrite>(uint8{1}, uint16{2}, uint32{3}, int64{-4});
	client.Process(0);
	Feed(client, "OK\r\n");
	client.Process(0);
	AT_TEXT(out.text, "AT+CWMODE=2\r\nAT+SIZES=1,2,3,-4\r\n");
	AT_TEXT(handled, "cwmode ok\nsizes write ok\n");
}


void TestWriteStringsAndOptionals() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Write<"+JOIN", &OnJoin>(std::string_view("a\"b,c\\"), std::optional<std::string_view>(std::string_view("pw")));
	client.Process(0);
	AT_TEXT(out.text, R"(AT+JOIN="a\"b,c\\","pw")" "\r\n");
	Feed(client, "OK\r\n");
	client.Process(0);

	client.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>());
	client.Process(0);
	AT_TEXT(out.text, R"(AT+JOIN="a\"b,c\\","pw")" "\r\n" R"(AT+JOIN="net")" "\r\n");
	Feed(client, "OK\r\n");
	client.Process(0);
	AT_TEXT(handled, "join ok\njoin ok\n");
}


void TestWriteTooLongStringIsRejected() {
	Reset();
	CapturePrint out;
	Client client(out);

	AtStatus status = client.Write<"+JOIN", &OnJoin>(std::string_view(std::string(33, 'x')), std::optional<std::string_view>());
	AT_EQ(status.status == ResultStatus::invalidParameter, true);
	client.Process(0);
	AT_TEXT(out.text, "");

	status = client.Write<"+JOIN", &OnJoin>(std::string_view(std::string(32, 'x')), std::optional<std::string_view>());
	AT_EQ(status.status == ResultStatus::ok, true);
	client.Process(0);
	AT_TEXT(out.text, "AT+JOIN=\"" + std::string(32, 'x') + "\"\r\n");
}


void TestTestForm() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Test<"+CWMODE", &OnTest>();
	client.Process(0);
	AT_TEXT(out.text, "AT+CWMODE=?\r\n");
	Feed(client, "+CWMODE: (0-1)\r\n\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "test ok\n");
}


void TestBasicValue() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Value<"E", &OnValue>(uint8{0});
	client.Process(0);
	AT_TEXT(out.text, "ATE0\r\n");
	Feed(client, "OK\r\n");
	client.Process(0);
	AT_TEXT(handled, "value ok\n");
}


// The dialect's echo builtin is a name of the set: a client sends ATE0 with Value<"E"> to turn echo off.
using EchoSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Echo())
		.Command<"+TEMP">().Read<int16>("celsius");
}>;


using EchoSetClient = AtClient<EchoSet, AtClientBinding<>>;


void TestBuiltinEchoValue() {
	Reset();
	CapturePrint out;
	EchoSetClient client(out);

	client.Value<"E", &OnValue>(uint8{0});
	client.Process(0);
	AT_TEXT(out.text, "ATE0\r\n");
	Feed(client, "OK\r\n");
	client.Process(0);
	AT_TEXT(handled, "value ok\n");
}


void TestReplyPrefixAndStrings() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+CIFSR", &OnIp>();
	client.Process(0);
	AT_TEXT(out.text, "AT+CIFSR?\r\n");
	Feed(client, "+CIFSR:STAIP \"192.168.4.2\"\r\n\r\nOK\r\n");
	client.Process(0);

	client.Read<"+NAME", &OnName>();
	client.Process(0);
	Feed(client, "+NAME: \"a\\\"b\"\r\nOK\r\n");
	client.Process(0);

	client.Read<"+NAME", &OnName>();
	client.Process(0);
	Feed(client, "+NAME: \"abcdefghi\"\r\nOK\r\n");
	client.Process(0);

	client.Read<"+NAME", &OnName>();
	client.Process(0);
	Feed(client, "+NAME: \"ab\" x\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "ip 192.168.4.2\nname a\"b\nname invalidData\nname invalidData\n");
}


void TestUrcBetweenInfoLines() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "WIFI GOT IP\r\n+TEMP: 7\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "got ip\ntemp 7\n");
}


void TestUrcArguments() {
	Reset();
	CapturePrint out;
	Client client(out);

	Feed(client, "+IPD,4,\"ab\",1\r\n");
	client.Process(0);
	Feed(client, "+IPD,4,\"a,b\"\r\n");
	client.Process(0);
	Feed(client, "+IPD,1,\"q\\\"\",0\r\n");
	client.Process(0);
	Feed(client, "+IPD,4\r\n");
	client.Process(0);
	Feed(client, "+IPD,x,\"a\",1\r\n");
	client.Process(0);
	Feed(client, "+IPD,4,\"a\",1,9\r\n");
	client.Process(0);
	Feed(client, "+IPD:4,\"a\",1\r\n");
	client.Process(0);
	Feed(client, "BELL\r\n");
	client.Process(0);
	AT_TEXT(handled, "ipd 4 ab true\nipd 4 a,b absent\nipd 1 q\" false\n");
}


void TestUrcWithoutRequest() {
	Reset();
	CapturePrint out;
	Client client(out);

	Feed(client, "WIFI GOT IP\r\n");
	client.Process(0);
	AT_TEXT(handled, "got ip\n");
	AT_TEXT(out.text, "");
}


// The echo module drops the echoed request. Without it the echo is an unknown line. The result is the same either way.
void TestEchoWithAndWithoutModule() {
	Reset();
	CapturePrint out;
	EchoClient echoing(out);

	echoing.Read<"+TEMP", &OnTemp>();
	echoing.Process(0);
	Feed(echoing, "AT+TEMP?\r\n+TEMP: 3\r\nOK\r\n");
	echoing.Process(0);
	AT_TEXT(handled, "temp 3\n");

	Reset();
	CapturePrint plainOut;
	Client plain(plainOut);

	plain.Read<"+TEMP", &OnTemp>();
	plain.Process(0);
	Feed(plain, "AT+TEMP?\r\n+TEMP: 3\r\nOK\r\n");
	plain.Process(0);
	AT_TEXT(handled, "temp 3\n");
}


// The echoed request is dropped only as the first line after the request. A later line equal to it is an unknown line.
void TestEchoOnlyFirstLine() {
	Reset();
	CapturePrint out;
	EchoClient client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "AT+TEMP?\r\nAT+TEMP?\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp invalidData\n");
}


void TestQueueOrderAndBusy() {
	Reset();
	CapturePrint out;
	Client client(out);

	AtStatus first = client.Read<"+TEMP", &OnTemp>();
	AtStatus second = client.Run<"+RUN", &OnRun>();
	AtStatus third = client.Run<"+RUN", &OnRun>();
	AT_EQ(first.status == ResultStatus::ok, true);
	AT_EQ(second.status == ResultStatus::ok, true);
	AT_EQ(third.status == ResultStatus::busy, true);
	client.Process(0);
	AT_TEXT(out.text, "AT+TEMP?\r\n");

	Feed(client, "+TEMP: 1\r\nOK\r\n");
	client.Process(0);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT+RUN\r\n");

	Feed(client, "OK\r\n");
	client.Process(0);
	AT_TEXT(handled, "temp 1\nrun ok\n");
}


void TestDroppedBytes() {
	Reset();
	CapturePrint out;
	Client client(out);

	for (int index = 0; index < 70; index++) {
		client.Feed(static_cast<uint8>('x'));
	}

	AT_EQ(client.DroppedBytes(), 6);
}


// A timeout at the deadline: 99 ms is not yet, 100 ms is. The drain sends AT, and its own timeout ends it.
void TestTimeoutAtDeadlineAndDrainEnd() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	client.Process(99);
	AT_TEXT(out.text, "AT+TEMP?\r\n");
	client.Process(100);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");
	AT_TEXT(handled, "");
	client.Process(199);
	AT_TEXT(handled, "");
	client.Process(200);
	AT_TEXT(handled, "temp timeout\n");
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");
}


// The late lines during the drain are dropped. The drain's own OK ends it. The next request is sent after it.
void TestLateFinalDuringDrainIsDropped() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Run<"+RUN", &OnRun>();
	client.Process(0);
	client.Process(100);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");

	Feed(client, "+TEMP: 9\r\n+CME ERROR: 3\r\nERROR\r\n", 100);
	AT_TEXT(handled, "");
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");

	Feed(client, "OK\r\n", 100);
	AT_TEXT(handled, "temp timeout\n");
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\nAT+RUN\r\n");

	Feed(client, "OK\r\n", 100);
	AT_TEXT(handled, "temp timeout\nrun ok\n");
}


// A module that never answers the drain's AT ends the drain at its own timeout, and the next request is sent then.
void TestSilentModuleEndsDrain() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Run<"+RUN", &OnRun>();
	client.Process(0);
	client.Process(100);
	client.Process(199);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");
	client.Process(200);
	AT_TEXT(handled, "temp timeout\n");
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\nAT+RUN\r\n");
}


// A first request stays open, a second waits behind it, and a timeout on the first starts the second.
void TestTimeoutStartsNextRequest() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Run<"+RUN", &OnRun>();
	client.Process(0);
	AT_TEXT(out.text, "AT+TEMP?\r\n");
	client.Process(50);
	AT_TEXT(out.text, "AT+TEMP?\r\n");
	client.Process(100);
	client.Process(200);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\nAT+RUN\r\n");
	AT_TEXT(handled, "temp timeout\n");
}


// The 32-bit wrap: a request sent 16 ms before the counter wraps times out 100 ms after it was sent, not before.
void TestTimeoutAcrossWrap() {
	Reset();
	CapturePrint out;
	Client client(out);

	const uint32 start = 0xFFFFFFF0u;

	client.Read<"+TEMP", &OnTemp>();
	client.Process(start);
	client.Process(start + 99u);
	AT_TEXT(handled, "");
	client.Process(start + 100u);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT\r\n");
	AT_TEXT(handled, "");

	const uint32 drainEnd = start + 100u + 100u;
	client.Process(drainEnd);
	AT_TEXT(handled, "temp timeout\n");
}


// A final line that arrives in the same Process call as the deadline is taken first, so the request gets its reply.
void TestFinalBeforeDeadlineInSameCall() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: 5\r\nOK\r\n");
	client.Process(100);
	AT_TEXT(handled, "temp 5\n");
	AT_TEXT(out.text, "AT+TEMP?\r\n");
}


// A request queued after a final line goes out on the next Process call, and the queue has room again.
void TestQueueAfterFinal() {
	Reset();
	CapturePrint out;
	Client client(out);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: 2\r\nOK\r\n");
	client.Process(0);
	client.Run<"+RUN", &OnRun>();
	client.Process(0);
	AT_TEXT(out.text, "AT+TEMP?\r\nAT+RUN\r\n");
}


}


int main() {
	TestReadOk();
	TestReadIntegerTypes();
	TestReadLinesThatDoNotParse();
	TestSecondValueIsInvalid();
	TestUnknownAndEmptyLinesIgnored();
	TestInfoLineTooLongIsInvalid();
	TestCmeAndErrorFinals();
	TestFinalWithoutInfoIsInvalidData();
	TestFailFinalAndDeclaredFinal();
	TestWriteBool();
	TestWriteIntegersAndSigned();
	TestWriteStringsAndOptionals();
	TestWriteTooLongStringIsRejected();
	TestTestForm();
	TestBasicValue();
	TestBuiltinEchoValue();
	TestReplyPrefixAndStrings();
	TestUrcBetweenInfoLines();
	TestUrcArguments();
	TestUrcWithoutRequest();
	TestEchoWithAndWithoutModule();
	TestEchoOnlyFirstLine();
	TestQueueOrderAndBusy();
	TestDroppedBytes();
	TestTimeoutAtDeadlineAndDrainEnd();
	TestLateFinalDuringDrainIsDropped();
	TestSilentModuleEndsDrain();
	TestTimeoutStartsNextRequest();
	TestTimeoutAcrossWrap();
	TestFinalBeforeDeadlineInSameCall();
	TestQueueAfterFinal();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtClient: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtClient: all %d checks passed\n", checkCount);
	return 0;
}
