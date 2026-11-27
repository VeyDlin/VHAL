#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <string_view>

// Loopback tests: one description, a server and a client, joined by two in-memory wires. Every form and every argument type goes both
// ways, CME and failure replies, URCs from the server between replies, and the echo that the server sends back. The same session runs
// with one byte per chunk and with random chunk sizes from fixed seeds. Each run must give the same handler log, the same wire bytes and
// the same application state, and the log must match the one written here from the design.


namespace {


int failureCount = 0;
int checkCount = 0;


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
			"TestAtLoopback.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
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
		std::fprintf(stderr, "TestAtLoopback.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// The application of the server: the state its handlers read and change.
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
};


bool operator==(const App& left, const App& right) {
	return left.temperature == right.temperature && left.led == right.led && left.mode == right.mode && left.ssid == right.ssid &&
		left.password == right.password && left.hasPassword == right.hasPassword && left.sizeTotal == right.sizeTotal &&
		left.lastLong == right.lastLong && left.basicValue == right.basicValue;
}


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
	String<8> name;
	name.characters[0] = 'a';
	name.characters[1] = 'b';
	name.characters[2] = 'c';
	name.length = 3;
	return name;
}


AtStatus RunPlain(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{};
}


AtStatus Deny(AtCall<App> call) {
	static_cast<void>(call);
	return AtCmeError(3);
}


AtStatus Bad(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{ResultStatus::error};
}


AtStatus BasicX(AtCall<App> call, uint8 on) {
	call.app.basicValue = on;
	return AtStatus{};
}


// The description: echo on, verbose results, CME errors in the verbose form with the one text the set declares, and every form.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(500))
		.Dialect(AtDialect().Echo().Verbose().CmeErrors(AtCmeMode::verbose).CmeText<3, "operation not allowed">())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+CWMODE">().Test().Read<int8>("mode").Write(Argument<int8>("mode"))
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(2000)
		.Command<"+SIZES">().Read<uint32>("total").Write(Argument<uint8>("a"), Argument<uint16>("b"), Argument<uint32>("c"), Argument<int64>("d"))
		.Command<"+NAME">().Read<String<8>>("name")
		.Command<"+RUN">().Run()
		.Command<"+DENY">().Run()
		.Command<"+BAD">().Run()
		.Basic<"X">().Value(Argument<uint8>("on"))
		.Urc<"WIFI GOT IP">()
		.Urc<"+IPD">(Argument<uint16>("length"), Argument<String<8>>("tag"), Argument<bool>("flag"));
}>;


using ServerBinding = AtServerBinding<
	AtOn<"+TEMP", &Temp>,
	AtOn<"+LED", &Led>,
	AtOn<"+CWMODE", &ModeRead>,
	AtOn<"+CWMODE", &ModeWrite>,
	AtOn<"+JOIN", &Join>,
	AtOn<"+SIZES", &SizesRead>,
	AtOn<"+SIZES", &SizesWrite>,
	AtOn<"+NAME", &NameRead>,
	AtOn<"+RUN", &RunPlain>,
	AtOn<"+DENY", &Deny>,
	AtOn<"+BAD", &Bad>,
	AtOn<"X", &BasicX>
>;


// The client's handlers write to one log, one line each.
std::string handled;


void Note(const std::string& text) {
	handled += text;
	handled += "\n";
}


std::string Describe(AtStatus status) {
	if (status.cmeCode != 0) {
		return "error cme " + std::to_string(status.cmeCode);
	}

	if (status.failureCode != 0) {
		return std::string(status.status == ResultStatus::ok ? "ok" : "error") + " failure " + std::to_string(status.failureCode);
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
		default:
			return "other";
	}
}


void OnTemp(Result<int16, AtStatus> result) {
	Note(result.IsOk() ? "temp " + std::to_string(result.Value()) : "temp " + Describe(result.Error()));
}


void OnMode(Result<int8, AtStatus> result) {
	Note(result.IsOk() ? "mode " + std::to_string(result.Value()) : "mode " + Describe(result.Error()));
}


void OnSizes(Result<uint32, AtStatus> result) {
	Note(result.IsOk() ? "sizes read " + std::to_string(result.Value()) : "sizes read " + Describe(result.Error()));
}


void OnName(Result<String<8>, AtStatus> result) {
	Note(result.IsOk() ? "name " + std::string(result.Value().View()) : "name " + Describe(result.Error()));
}


void OnLed(AtStatus status) {
	Note("led " + Describe(status));
}


void OnJoin(AtStatus status) {
	Note("join " + Describe(status));
}


void OnTest(AtStatus status) {
	Note("test " + Describe(status));
}


void OnSizesWrite(AtStatus status) {
	Note("sizes write " + Describe(status));
}


void OnRun(AtStatus status) {
	Note("run " + Describe(status));
}


void OnValue(AtStatus status) {
	Note("value " + Describe(status));
}


void OnGotIp() {
	Note("got ip");
}


void OnIpd(uint16 length, std::string_view tag, bool flag) {
	Note("ipd " + std::to_string(length) + " " + std::string(tag) + " " + (flag ? "true" : "false"));
}


using ClientBinding = AtClientBinding<
	AtUrcOn<"WIFI GOT IP", &OnGotIp>,
	AtUrcOn<"+IPD", &OnIpd>
>;


// The output of one side: the bytes it wrote, in order.
struct Wire : public Print {
	std::string bytes;

	void WriteRaw(const char* data, size_t size) override {
		bytes.append(data, size);
	}
};


String<8> Text8(std::string_view text) {
	String<8> value;

	for (std::size_t index = 0; index < text.size(); index++) {
		value.characters[index] = text[index];
	}

	value.length = text.size();
	return value;
}


// The result of one session: what the client logged, what each side wrote, and the server's application state.
struct Outcome {
	std::string log;
	std::string clientWire;
	std::string serverWire;
	App app;
	std::size_t invalidCount = 0;
};


// How the bytes of a wire reach the other side: one byte at a time, or in chunks of random sizes from a fixed seed.
struct Delivery {
	bool oneByte = true;
	std::mt19937 random{1};


	std::size_t Chunk() {
		if (oneByte) {
			return 1;
		}

		return 1 + random() % 40;
	}
};


std::size_t CountLines(const std::string& text) {
	std::size_t lines = 0;

	for (char character : text) {
		if (character == '\n') {
			lines++;
		}
	}

	return lines;
}


// One session. Each request is issued, and the exchange runs until the client has logged its reply. A URC is queued while the link is
// idle, so it reaches the client between two replies.
Outcome RunSession(bool oneByte, unsigned seed) {
	handled.clear();

	App app;
	Wire toServer;
	Wire toClient;
	AtClient<Set, ClientBinding, AtQueue<2>, AtEcho> client(toServer);
	AtServer<Set, App, ServerBinding> server(app, toClient);
	Delivery delivery;
	delivery.oneByte = oneByte;
	delivery.random.seed(seed);

	std::string clientWire;
	std::string serverWire;

	// One exchange: the client writes its requests, the server reads them and writes its replies, and the client reads those. The bytes
	// of each wire are cut into chunks; each chunk is fed, and the receiver runs after it.
	auto pump = [&]() {
		client.Process(0);

		std::size_t position = 0;

		while (position < toServer.bytes.size()) {
			std::size_t chunk = std::min(delivery.Chunk(), toServer.bytes.size() - position);

			for (std::size_t index = 0; index < chunk; index++) {
				server.Feed(static_cast<uint8>(toServer.bytes[position + index]));
			}

			server.Process();
			position += chunk;
		}

		clientWire += toServer.bytes;
		toServer.bytes.clear();
		server.Process();

		position = 0;

		while (position < toClient.bytes.size()) {
			std::size_t chunk = std::min(delivery.Chunk(), toClient.bytes.size() - position);

			for (std::size_t index = 0; index < chunk; index++) {
				client.Feed(static_cast<uint8>(toClient.bytes[position + index]));
			}

			client.Process(0);
			position += chunk;
		}

		serverWire += toClient.bytes;
		toClient.bytes.clear();
		client.Process(0);
	};

	// Pumps until the client has logged the given number of lines. A session that does not finish is a failure, not a hang.
	auto drain = [&](std::size_t lines) {
		for (int round = 0; round < 50 && CountLines(handled) < lines; round++) {
			pump();
		}
	};

	// Runs one step and returns its status. A busy queue is pumped and tried again.
	auto issue = [&]<typename Call>(Call call) -> AtStatus {
		AtStatus status = call();

		while (status.status == ResultStatus::busy) {
			pump();
			status = call();
		}

		return status;
	};

	issue([&]() { return client.Read<"+TEMP", &OnTemp>(); });
	drain(1);
	issue([&]() { return client.Write<"+LED", &OnLed>(true); });
	drain(2);
	issue([&]() { return client.Test<"+CWMODE", &OnTest>(); });
	drain(3);
	issue([&]() {
		return client.Write<"+JOIN", &OnJoin>(std::string_view("my \"net\", x"), std::optional<std::string_view>(std::string_view("pw")));
	});
	drain(4);
	issue([&]() { return client.Write<"+SIZES", &OnSizesWrite>(uint8{1}, uint16{2}, uint32{3}, int64{-4}); });
	drain(5);
	issue([&]() { return client.Read<"+SIZES", &OnSizes>(); });
	drain(6);
	issue([&]() { return client.Read<"+NAME", &OnName>(); });
	drain(7);

	server.Urc<"WIFI GOT IP">();
	drain(8);
	issue([&]() { return client.Run<"+RUN", &OnRun>(); });
	drain(9);
	issue([&]() { return client.Run<"+DENY", &OnRun>(); });
	drain(10);

	server.Urc<"+IPD">(uint16{4}, Text8("ab"), true);
	drain(11);
	issue([&]() { return client.Run<"+BAD", &OnRun>(); });
	drain(12);
	issue([&]() { return client.Value<"X", &OnValue>(uint8{1}); });
	drain(13);
	issue([&]() { return client.Read<"+CWMODE", &OnMode>(); });
	drain(14);

	// A string longer than its capacity is refused at the call, so nothing reaches the wire for it.
	AtStatus refused = client.Write<"+JOIN", &OnJoin>(std::string_view(std::string(33, 'x')), std::optional<std::string_view>());
	pump();

	Outcome outcome;
	outcome.log = handled;
	outcome.clientWire = clientWire;
	outcome.serverWire = serverWire;
	outcome.app = app;
	outcome.invalidCount = refused.status == ResultStatus::invalidParameter ? 1 : 0;
	return outcome;
}


// The app state that the session must leave behind.
App ExpectedApp() {
	App app;
	app.temperature = 23;
	app.led = true;
	app.mode = 0;
	app.ssid = "my \"net\", x";
	app.password = "pw";
	app.hasPassword = true;
	app.sizeTotal = 6;
	app.lastLong = -4;
	app.basicValue = 1;
	return app;
}


// The log the session must produce, written from the design: each reply in order, a URC between the replies it was queued between.
const char* ExpectedLog =
	"temp 23\n"
	"led ok\n"
	"test ok\n"
	"join ok\n"
	"sizes write ok\n"
	"sizes read 6\n"
	"name abc\n"
	"got ip\n"
	"run ok\n"
	"run error cme 3\n"
	"ipd 4 ab true\n"
	"run error\n"
	"value ok\n"
	"mode 0\n";


void OnPing(AtStatus status) {
	Note("ping " + Describe(status));
}


void OnCwjap(AtStatus status) {
	Note("cwjap " + Describe(status));
}


// The description of the failure code: one write with a failure code line. The server's handler returns the code it is given.
struct FailureApp {
};


AtStatus FailureJoin(AtCall<FailureApp> call, uint8 code) {
	static_cast<void>(call);
	AtStatus status{ResultStatus::error};
	status.failureCode = code;
	return status;
}


using FailureSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(500))
		.Command<"+CWJAP">().Write(Argument<uint8>("code")).FailureCode("+CWJAP")
		.Urc<"WIFI GOT IP">();
}>;


using FailureServerBinding = AtServerBinding<
	AtOn<"+CWJAP", &FailureJoin>
>;


// Runs the two sides, one round at a time, until the rounds have passed: the client's bytes go to the server, and the server's to the
// client. The server's bytes are added to serverSaid, so a test can read them after the rounds.
template<typename ClientType, typename ServerType>
void Exchange(ClientType& client, ServerType& server, Wire& toServer, Wire& toClient, std::string& serverSaid) {
	for (int round = 0; round < 4; round++) {
		client.Process(0);

		for (char character : toServer.bytes) {
			server.Feed(static_cast<uint8>(character));
		}

		toServer.bytes.clear();
		server.Process();
		serverSaid += toClient.bytes;

		for (char character : toClient.bytes) {
			client.Feed(static_cast<uint8>(character));
		}

		toClient.bytes.clear();
		client.Process(0);
	}
}


// A Ping across the wire: the server answers the bare AT with OK, and the client's echo module drops the echo of its request.
void TestPingLoopback() {
	handled.clear();
	Wire toServer;
	Wire toClient;
	App app;
	AtClient<Set, ClientBinding, AtQueue<2>, AtEcho> client(toServer);
	AtServer<Set, App, ServerBinding> server(app, toClient);
	std::string serverSaid;

	AtStatus queued = client.Ping<&OnPing>();
	AT_EQ(queued.status == ResultStatus::ok, true);
	Exchange(client, server, toServer, toClient, serverSaid);
	AT_TEXT(handled, "ping ok\n");
}


// The failure code of a write: the server's handler returns error with code 2, so the server writes +CWJAP:2 before ERROR, and the client's
// status carries the code. The next request, with code 0, writes no code line, so its status has none.
void TestFailureCodeLoopback() {
	handled.clear();
	Wire toServer;
	Wire toClient;
	FailureApp app;
	AtClient<FailureSet, AtClientBinding<>, AtQueue<2>> client(toServer);
	AtServer<FailureSet, FailureApp, FailureServerBinding> server(app, toClient);
	std::string serverSaid;

	client.Write<"+CWJAP", &OnCwjap>(uint8{2});
	client.Write<"+CWJAP", &OnCwjap>(uint8{0});
	Exchange(client, server, toServer, toClient, serverSaid);
	AT_TEXT(handled, "cwjap error failure 2\ncwjap error\n");
	AT_TEXT(serverSaid, "\r\n+CWJAP:2\r\n\r\nERROR\r\n\r\nERROR\r\n");
}


// Two requests in flight at once: the second waits in the queue behind the first. A URC that the server queued during the first reply
// reaches the client between the two replies, and the second request is sent only after the first reply.
void TestPipelinedRequestsAndUrc() {
	handled.clear();

	Wire toServer;
	Wire toClient;
	App app;
	AtClient<Set, ClientBinding, AtQueue<2>, AtEcho> client(toServer);
	AtServer<Set, App, ServerBinding> server(app, toClient);

	AtStatus first = client.Read<"+TEMP", &OnTemp>();
	AtStatus second = client.Run<"+RUN", &OnRun>();
	AT_EQ(first.status == ResultStatus::ok, true);
	AT_EQ(second.status == ResultStatus::ok, true);
	server.Urc<"+IPD">(uint16{9}, Text8("zz"), false);

	for (int round = 0; round < 4; round++) {
		client.Process(0);

		for (char character : toServer.bytes) {
			server.Feed(static_cast<uint8>(character));
		}

		toServer.bytes.clear();
		server.Process();

		for (char character : toClient.bytes) {
			client.Feed(static_cast<uint8>(character));
		}

		toClient.bytes.clear();
		client.Process(0);
	}

	AT_TEXT(handled, "temp 23\nipd 9 zz false\nrun ok\n");
}


void TestSessionsAgree() {
	Outcome byByte = RunSession(true, 0);
	AT_TEXT(byByte.log, ExpectedLog);
	AT_EQ(byByte.app == ExpectedApp(), true);
	AT_EQ(byByte.invalidCount, 1);

	const unsigned seeds[] = {1, 7, 2024, 99991};

	for (unsigned seed : seeds) {
		Outcome chunked = RunSession(false, seed);
		AT_TEXT(chunked.log, byByte.log);
		AT_TEXT(chunked.clientWire, byByte.clientWire);
		AT_TEXT(chunked.serverWire, byByte.serverWire);
		AT_EQ(chunked.app == byByte.app, true);
		AT_EQ(chunked.invalidCount, byByte.invalidCount);
	}
}


}


int main() {
	TestPipelinedRequestsAndUrc();
	TestSessionsAgree();
	TestPingLoopback();
	TestFailureCodeLoopback();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtLoopback: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtLoopback: all %d checks passed\n", checkCount);
	return 0;
}
