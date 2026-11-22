#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// Host tests of the Await calls. The RTOS handle plays another task: each time WaitCondition yields, it advances the fake tick by one
// millisecond, runs Process() with that tick, and lets a scripted module answer each request line the client writes. Each script entry is
// the module's answer to one write, fed in chunks of at most 60 bytes, since the client's ring holds 64. Expected lines and times are
// written here by hand: the request is the design's AT line, a reply is the design's info line and final, and a time is the design's
// timeout plus the drain, or the safety limit of the queue.


namespace {


int failureCount = 0;
int checkCount = 0;


void ExpectText(const std::string& actual, const std::string& expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtAwait.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", line, expression, actual.c_str(), expected.c_str());
	}
}


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtAwait.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectTrue(bool condition, const char* expression, int line) {
	checkCount++;

	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAtAwait.cpp:%d: %s is false\n", line, expression);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define AT_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// The handler log of the client, one line for each call.
std::string handled;


void Note(const std::string& text) {
	handled += text;
	handled += "\n";
}


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
		default:
			return "other";
	}
}


// The output of the client, in the order it is written.
struct Wire : public Print {
	std::string bytes;

	void WriteRaw(const char* data, size_t size) override {
		bytes.append(data, size);
	}
};


Wire wire;


// The row of a network: an integer, a string and an integer.
struct Net {
	uint8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
};


constexpr auto NetRow = AtRow<Net>(
	AtField<&Net::ecn>("ecn"),
	AtField<&Net::ssid>("ssid"),
	AtField<&Net::rssi>("rssi")
);


// The description: a default of 100 ms, a read of a value and of a string, a run with a value and one without, a write, the builtin E with
// echo, a basic command X, a rows run and a rows read, a prompt command with its finals, and a command of 300 ms. The longest timeout is 300,
// so the safety limit of a queue of two is 2 * (300 + 100) = 800 ms.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().Echo())
		.Modules(AtDataMode)
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+NAME">().Read<String<8>>("name")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+RST">().Run()
		.Command<"+GMR">().Run<int16>("version")
		.Command<"+LIST">().Rows(NetRow)
		.Command<"+SCAN">().ReadRows(NetRow)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK").Failures("SEND FAIL")
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid")).TimeoutMs(300)
		.Basic<"X">().Value(Argument<uint8>("on"));
}>;


using Client = AtClient<Set, AtClientBinding<>, AtQueue<2>, AtRows<4>, AtAwait>;


Client* gClient = nullptr;

std::vector<std::string> script;
std::size_t scriptNext = 0;
std::size_t fedWire = 0;

// How many more times the RTOS handle runs Process(): -1 for every time, 0 for none.
int processRuns = -1;


void Feed(const std::string& text) {
	std::size_t fed = 0;

	for (char character : text) {
		gClient->Feed(static_cast<uint8>(character));
		fed++;

		if (fed % 60 == 0) {
			gClient->Process(System::GetTick32());
		}
	}
}


// The module answers the request that the client has just written: the next script entry is fed, once for each new write.
void Answer() {
	if (wire.bytes.size() == fedWire) {
		return;
	}

	fedWire = wire.bytes.size();

	if (scriptNext < script.size()) {
		Feed(script[scriptNext]);
		scriptNext++;
	}
}


// The RTOS handle of the tests: another task. One millisecond passes, the client runs with the tick, and the module answers.
bool RtosTask(uint32 delay) {
	System::tickLow = System::tickLow + delay;

	if (processRuns != 0 && gClient != nullptr) {
		if (processRuns > 0) {
			processRuns--;
		}

		gClient->Process(System::GetTick32());
		Answer();
	}

	return true;
}


// Starts a test: the client, the script of the module, and empty logs.
void Reset(Client* target, std::vector<std::string> replies) {
	wire.bytes.clear();
	fedWire = 0;
	script = replies;
	scriptNext = 0;
	gClient = target;
	processRuns = -1;
	handled.clear();
}


void OnTemp(Result<int16, AtStatus> result) {
	Note(result.IsOk() ? "temp " + std::to_string(result.Value()) : "temp " + Describe(result.Error()));
}


void OnStatus(AtStatus status) {
	Note("status " + Describe(status));
}


// Advances the tick and runs the client with it, without a module, for count milliseconds.
void Advance(Client& client, uint32 count) {
	for (uint32 index = 0; index < count; index++) {
		System::tickLow = System::tickLow + 1;
		client.Process(System::GetTick32());
	}
}


// The values and statuses of each form, with the module's answer for each.
void TestAwaitForms() {
	Client client(wire);
	Reset(&client, {"+TEMP: 23\r\nOK\r\n"});
	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsOk());
	AT_EQ(temp.Value(), 23);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\n");

	Reset(&client, {"+NAME: \"abc\"\r\nOK\r\n"});
	Result<String<8>, AtStatus> name = client.AwaitRead<"+NAME">();
	AT_TRUE(name.IsOk());
	AT_EQ(name.Value().length, 3);
	AT_TRUE(name.Value().characters[0] == 'a' && name.Value().characters[2] == 'c');

	Reset(&client, {"+GMR: 7\r\nOK\r\n"});
	Result<int16, AtStatus> version = client.AwaitRun<"+GMR">();
	AT_TRUE(version.IsOk());
	AT_EQ(version.Value(), 7);
	AT_TEXT(wire.bytes, "AT+GMR\r\n");

	Reset(&client, {"OK\r\n"});
	AtStatus run = client.AwaitRun<"+RST">();
	AT_TEXT(Describe(run), "ok");
	AT_TEXT(wire.bytes, "AT+RST\r\n");

	Reset(&client, {"OK\r\n"});
	AtStatus written = client.AwaitWrite<"+LED">(true);
	AT_TEXT(Describe(written), "ok");
	AT_TEXT(wire.bytes, "AT+LED=1\r\n");

	Reset(&client, {"OK\r\n"});
	AtStatus builtin = client.AwaitValue<"E">(uint8{0});
	AT_TEXT(Describe(builtin), "ok");
	AT_TEXT(wire.bytes, "ATE0\r\n");

	Reset(&client, {"OK\r\n"});
	AtStatus basic = client.AwaitValue<"X">(uint8{3});
	AT_TEXT(Describe(basic), "ok");
	AT_TEXT(wire.bytes, "ATX3\r\n");

	Reset(&client, {"ERROR\r\n"});
	Result<int16, AtStatus> failed = client.AwaitRead<"+TEMP">();
	AT_TRUE(failed.IsErr());
	AT_TEXT(Describe(failed.Error()), "error");

	Reset(&client, {"+CME ERROR: 3\r\n"});
	Result<int16, AtStatus> cme = client.AwaitRead<"+TEMP">();
	AT_TRUE(cme.IsErr());
	AT_TEXT(Describe(cme.Error()), "error cme 3");

	AT_EQ(simCriticalDepth, 0);
}


// Rows: five rows into four stored rows, with one skipped. The copy stays valid after a later request. A read of rows.
void TestAwaitRows() {
	Client client(wire);
	Reset(
		&client,
		{
			"+LIST: 1,\"aa\",-50\r\n+LIST: 2,\"bb\",-60\r\n+LIST: 3,\"cc\",-70\r\n+LIST: 4,\"dd\",-80\r\n+LIST: 5,\"ee\",-90\r\n\r\nOK\r\n",
			"+TEMP: 1\r\nOK\r\n",
			"+SCAN: 9,\"x\",-1\r\nOK\r\n"
		});

	Result<AtRowCopy<Net, 4>, AtStatus> list = client.AwaitRows<"+LIST">();
	AT_TRUE(list.IsOk());
	AT_EQ(list.Value().Count(), 4);
	AT_EQ(list.Value().Skipped(), 1);
	AT_TRUE(list.Value()[0].ssid.characters[0] == 'a');
	AT_EQ(list.Value()[3].rssi, -80);
	AT_EQ(client.DroppedBytes(), 0);
	AT_TEXT(wire.bytes, "AT+LIST\r\n");

	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsOk());
	AT_EQ(list.Value().Count(), 4);
	AT_TRUE(list.Value()[0].ssid.characters[1] == 'a');
	AT_EQ(list.Value()[1].rssi, -60);

	Result<AtRowCopy<Net, 4>, AtStatus> scan = client.AwaitReadRows<"+SCAN">();
	AT_TRUE(scan.IsOk());
	AT_EQ(scan.Value().Count(), 1);
	AT_EQ(scan.Value()[0].ecn, 9);
	AT_EQ(scan.Value()[0].rssi, -1);
	AT_TEXT(wire.bytes, "AT+LIST\r\nAT+TEMP?\r\nAT+SCAN?\r\n");

	AT_EQ(simCriticalDepth, 0);
}


// The prompt: the request line, then the data after the '>' of the module, then the final line.
void TestAwaitSendData() {
	static constexpr uint8 payload[] = {'h', 'e', 'l', 'l', 'o'};
	Client client(wire);
	Reset(&client, {"OK\r\n>", "Recv 5 bytes\r\n\r\nSEND OK\r\n"});

	AtStatus sent = client.AwaitSendData<"+CIPSEND">(uint16{5}, std::span<const uint8>(payload));
	AT_TEXT(Describe(sent), "ok");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhello");

	Reset(&client, {"OK\r\n>", "SEND FAIL\r\n"});
	AtStatus failed = client.AwaitSendData<"+CIPSEND">(uint16{5}, std::span<const uint8>(payload));
	AT_TEXT(Describe(failed), "error");

	AT_EQ(simCriticalDepth, 0);
}


// A request queued behind another: the first is a plain call, and the Await waits until the second one is answered.
void TestAwaitBehindAnother() {
	Client client(wire);
	Reset(&client, {"+TEMP: 1\r\nOK\r\n", "+NAME: \"abc\"\r\nOK\r\n"});
	client.Read<"+TEMP", &OnTemp>();

	Result<String<8>, AtStatus> name = client.AwaitRead<"+NAME">();
	AT_TRUE(name.IsOk());
	AT_EQ(name.Value().length, 3);
	AT_TEXT(handled, "temp 1\n");
	AT_TEXT(wire.bytes, "AT+TEMP?\r\nAT+NAME?\r\n");
	AT_EQ(simCriticalDepth, 0);
}


// A full queue: the Await returns busy at once, without waiting, and writes nothing.
void TestAwaitBusyWhenFull() {
	Client client(wire);
	Reset(&client, {});
	client.Write<"+LED", &OnStatus>(true);
	client.Run<"+RST", &OnStatus>();

	uint32 before = System::GetTick32();
	AtStatus written = client.AwaitWrite<"+LED">(false);
	AT_TEXT(Describe(written), "busy");

	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsErr());
	AT_TEXT(Describe(temp.Error()), "busy");

	AT_EQ(System::GetTick32() - before, 0);
	AT_TEXT(wire.bytes, "");
	AT_EQ(simCriticalDepth, 0);
}


// The module never answers: the request times out at 100 ms and its drain ends 100 ms later, so the Await returns timeout after 201 ms. The
// start is near the 32-bit wrap, so the elapsed time is measured across it, with the same clock as Process().
void TestAwaitModuleTimeout() {
	Client client(wire);
	Reset(&client, {});
	System::tickLow = 0xFFFFFFF0u;
	uint32 start = System::GetTick32();

	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsErr());
	AT_TEXT(Describe(temp.Error()), "timeout");
	AT_EQ(System::GetTick32() - start, 201);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\nAT\r\n");
	AT_EQ(simCriticalDepth, 0);
}


// No Process() runs: the Await waits for the safety limit, 800 ms, and abandons its entry, which is never sent. A later Process() frees it
// without a call and without writing it. The next request then works.
void TestAwaitSafetyLimitNoProcess() {
	Client client(wire);
	Reset(&client, {"+TEMP: 9\r\nOK\r\n"});
	processRuns = 0;
	uint32 start = System::GetTick32();

	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsErr());
	AT_TEXT(Describe(temp.Error()), "timeout");
	AT_EQ(System::GetTick32() - start, 800);
	AT_TEXT(wire.bytes, "");

	processRuns = -1;
	Advance(client, 1);
	AT_TEXT(wire.bytes, "");
	AT_TEXT(handled, "");

	temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsOk());
	AT_EQ(temp.Value(), 9);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\n");
	AT_TEXT(handled, "");
	AT_EQ(simCriticalDepth, 0);
}


// The request was sent, and then no Process() runs: the Await gives up at 800 ms and abandons the open entry. Process() then drains it, and
// the drain's end frees the entry with no handler call. The next request works.
void TestAwaitSafetyLimitOpenRequest() {
	Client client(wire);
	Reset(&client, {});
	processRuns = 1;
	uint32 start = System::GetTick32();

	Result<int16, AtStatus> temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsErr());
	AT_TEXT(Describe(temp.Error()), "timeout");
	AT_EQ(System::GetTick32() - start, 800);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\n");

	processRuns = -1;
	Advance(client, 150);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\nAT\r\n");
	AT_TEXT(handled, "");

	script = {"+TEMP: 4\r\nOK\r\n"};
	scriptNext = 0;
	temp = client.AwaitRead<"+TEMP">();
	AT_TRUE(temp.IsOk());
	AT_EQ(temp.Value(), 4);
	AT_TEXT(wire.bytes, "AT+TEMP?\r\nAT\r\nAT+TEMP?\r\n");
	AT_TEXT(handled, "");
	AT_EQ(simCriticalDepth, 0);
}


// Wires the RTOS handle, which the Await waits use.
void InstallHandle() {
	System::rtosDelayMsHandle = RtosTask;
}


}


int main() {
	InstallHandle();
	TestAwaitForms();
	TestAwaitRows();
	TestAwaitSendData();
	TestAwaitBehindAnother();
	TestAwaitBusyWhenFull();
	TestAwaitModuleTimeout();
	TestAwaitSafetyLimitNoProcess();
	TestAwaitSafetyLimitOpenRequest();

	if (failureCount != 0) {
		std::printf("%d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtAwait: all %d checks passed\n", checkCount);
	return 0;
}
