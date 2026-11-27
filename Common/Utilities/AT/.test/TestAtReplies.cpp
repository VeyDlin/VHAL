#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
#include <algorithm>
#include <cstdio>
#include <optional>
#include <random>
#include <string>
#include <string_view>

// Reply loopback tests: one description with the reply forms, a server and a client joined by two in-memory wires. The forms are a value
// (Read and Run with a type), a record (Read and Run with a row), a run form with a reply prefix that is followed by a comma, raw text (a
// RawString value and a RawString field), a rows reply (Rows, with a capacity of five for eight rows), a visit of the rows (RowsVisit), and
// a read of rows (ReadRows and ReadRowsVisit). Each session runs byte by byte and with random chunks from fixed seeds; every run must give
// the handler log that the design gives, and the same wires.


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
			"TestAtReplies.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
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
		std::fprintf(stderr, "TestAtReplies.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)


// The wire: the text that one side writes, in order.
struct Wire : public Print {
	std::string bytes;

	void WriteRaw(const char* data, size_t size) override {
		bytes.append(data, size);
	}
};


// The records and the rows of the description.
struct Network {
	int8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
	String<17> mac{};
	uint8 channel = 0;
};


struct Join {
	String<32> ssid{};
	int8 rssi = 0;
	std::optional<String<64>> password{};
};


struct Status {
	int8 code = 0;
	RawString<16> text{};
};


template<std::size_t Capacity>
String<Capacity> ToString(std::string_view text) {
	String<Capacity> value;

	for (char character : text) {
		value.characters[value.length] = character;
		value.length++;
	}

	return value;
}


template<std::size_t Capacity>
RawString<Capacity> ToRaw(std::string_view text) {
	RawString<Capacity> value;

	for (char character : text) {
		value.characters[value.length] = character;
		value.length++;
	}

	return value;
}


// The row of a network: an integer, a string, an integer with a range, a string, and an integer. Its Open and Close are the parentheses.
constexpr auto NetworkRow = AtRow<Network>(
	AtField<&Network::ecn>("ecn"),
	AtField<&Network::ssid>("ssid"),
	AtField<&Network::rssi>("rssi").Range(-100, 0),
	AtField<&Network::mac>("mac"),
	AtField<&Network::channel>("channel")
).Open("(").Close(")");


// A join: the optional password is the last field.
constexpr auto JoinRow = AtRow<Join>(
	AtField<&Join::ssid>("ssid"),
	AtField<&Join::rssi>("rssi").Range(-120, 0),
	AtField<&Join::password>("password")
);


// A status: the raw text is the last field and ends at the Close text.
constexpr auto StatusRow = AtRow<Status>(AtField<&Status::code>("code"), AtField<&Status::text>("text")).Open("<").Close(">");


// The description: each reply form once, and the two rows of a rows reply and a read of rows.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(500))
		.Dialect(AtDialect().Echo())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+JOIN">().Write(Argument<String<32>>("ssid"), Optional<String<64>>("password")).TimeoutMs(2000)
		.Command<"+JOINED">().Read(JoinRow)
		.Command<"+STAT">().Run(StatusRow)
		.Command<"+CIFSR">().Run<String<15>>("ip").Reply("+CIFSR:STAIP")
		.Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
		.Command<"+LIST">().Rows(NetworkRow)
		.Command<"+SCAN">().ReadRows(NetworkRow)
		.Command<"+RUN">().Run()
		.Urc<"WIFI GOT IP">();
}>;


// The application of the server: what its handlers read and change.
struct App {
	int16 temperature = 23;
	std::string ssid;
	bool hasPassword = false;
	std::string password;
};


Result<int16, AtStatus> Temp(AtCall<App> call) {
	return call.app.temperature;
}


AtStatus JoinWrite(AtCall<App> call, std::string_view ssid, std::optional<std::string_view> password) {
	call.app.ssid = std::string(ssid);
	call.app.hasPassword = password.has_value();
	call.app.password = password ? std::string(*password) : std::string();
	return AtStatus{};
}


Result<Join, AtStatus> JoinRead(AtCall<App> call) {
	Join join;
	join.ssid = ToString<32>(call.app.ssid);
	join.rssi = -40;

	if (call.app.hasPassword) {
		join.password = ToString<64>(call.app.password);
	}

	return join;
}


Result<Status, AtStatus> StatusRun(AtCall<App> call) {
	static_cast<void>(call);

	Status status;
	status.code = 3;
	status.text = ToRaw<16>("hello world");
	return status;
}


Result<String<15>, AtStatus> IpRun(AtCall<App> call) {
	static_cast<void>(call);
	return ToString<15>("192.168.1.9");
}


Result<RawString<64>, AtStatus> VersionRun(AtCall<App> call) {
	static_cast<void>(call);
	return ToRaw<64>("2.2.0.0(Jul 1 2022)");
}


// The network of row k: the third has a quoted ")" and "," in its text, so the client must honour the quotes.
Network NetworkAt(int index) {
	Network network;
	network.ecn = static_cast<int8>(index % 5);
	network.ssid = index == 2 ? ToString<32>("a),b") : ToString<32>("n" + std::to_string(index));
	network.rssi = static_cast<int8>(-(40 + index));
	network.mac = ToString<17>("aa:bb");
	network.channel = static_cast<uint8>(index + 1);
	return network;
}


AtStatus ListRun(AtCall<App> call, AtRowWriter<Network>& out) {
	static_cast<void>(call);

	for (int index = 0; index < 8; index++) {
		out.Add(NetworkAt(index));
	}

	return AtStatus{};
}


AtStatus ScanRead(AtCall<App> call, AtRowWriter<Network>& out) {
	static_cast<void>(call);
	out.Add(NetworkAt(0));
	out.Add(NetworkAt(1));
	return AtStatus{};
}


AtStatus RunPlain(AtCall<App> call) {
	static_cast<void>(call);
	return AtStatus{};
}


using ServerBinding = AtServerBinding<
	AtOn<"+TEMP", &Temp>,
	AtOn<"+JOIN", &JoinWrite>,
	AtOn<"+JOINED", &JoinRead>,
	AtOn<"+STAT", &StatusRun>,
	AtOn<"+CIFSR", &IpRun>,
	AtOn<"+GMR", &VersionRun>,
	AtOn<"+LIST", &ListRun>,
	AtOn<"+SCAN", &ScanRead>,
	AtOn<"+RUN", &RunPlain>
>;


// The client's handlers write one log line each.
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
		case ResultStatus::invalidParameter:
			return "invalidParameter";
		default:
			return "other";
	}
}


std::string Text(std::string_view text) {
	return std::string(text);
}


void OnJoin(AtStatus status) {
	Note("join " + Describe(status));
}


void OnJoined(Result<Join, AtStatus> result) {
	if (result.IsErr()) {
		Note("joined " + Describe(result.Error()));
		return;
	}

	const Join& join = result.Value();
	std::string password = join.password.has_value() ? Text(join.password->View()) : std::string("absent");
	Note("joined " + Text(join.ssid.View()) + " " + std::to_string(join.rssi) + " " + password);
}


void OnTemp(Result<int16, AtStatus> result) {
	Note(result.IsOk() ? "temp " + std::to_string(result.Value()) : "temp " + Describe(result.Error()));
}


void OnStat(Result<Status, AtStatus> result) {
	if (result.IsErr()) {
		Note("stat " + Describe(result.Error()));
		return;
	}

	Note("stat " + std::to_string(result.Value().code) + " " + Text(result.Value().text.View()));
}


void OnIp(Result<String<15>, AtStatus> result) {
	Note(result.IsOk() ? "ip " + Text(result.Value().View()) : "ip " + Describe(result.Error()));
}


void OnVersion(Result<RawString<64>, AtStatus> result) {
	Note(result.IsOk() ? "version " + Text(result.Value().View()) : "version " + Describe(result.Error()));
}


// The names of the rows that a list holds, separated by bars.
std::string NamesOf(const AtRowList<Network, 5>& list) {
	std::string names;

	for (const Network& network : list) {
		if (!names.empty()) {
			names += "|";
		}

		names += Text(network.ssid.View());
	}

	return names;
}


void OnList(const Result<AtRowList<Network, 5>, AtStatus>& result) {
	if (result.IsErr()) {
		Note("list " + Describe(result.Error()));
		return;
	}

	const AtRowList<Network, 5>& list = result.Value();
	Note("list " + std::to_string(list.Count()) + " " + std::to_string(list.Skipped()) + " " + NamesOf(list));
}


void OnScan(const Result<AtRowList<Network, 5>, AtStatus>& result) {
	if (result.IsErr()) {
		Note("scan " + Describe(result.Error()));
		return;
	}

	const AtRowList<Network, 5>& list = result.Value();
	Note("scan " + std::to_string(list.Count()) + " " + std::to_string(list.Skipped()) + " " + NamesOf(list));
}


void OnRow(const Network& network) {
	Note("row " + Text(network.ssid.View()) + " " + std::to_string(network.rssi));
}


void OnRowsDone(AtStatus status) {
	Note("rows done " + Describe(status));
}


void OnRun(AtStatus status) {
	Note("run " + Describe(status));
}


void OnGotIp() {
	Note("got ip");
}


using ClientBinding = AtClientBinding<AtUrcOn<"WIFI GOT IP", &OnGotIp>>;


// One session: the requests in order, each waiting for its reply before the next, with the module's bytes in chunks of the given sizes.
struct Outcome {
	std::string log;
	std::string clientWire;
	std::string serverWire;
};


Outcome RunSession(bool byByte, unsigned seed) {
	handled.clear();

	Wire toServer;
	Wire toClient;
	App app;
	AtClient<Set, ClientBinding, AtQueue<2>, AtEcho, AtRows<5>> client(toServer);
	AtServer<Set, App, ServerBinding> server(app, toClient);
	std::mt19937 random(seed);

	std::string clientWire;
	std::string serverWire;

	// One exchange: the client writes, the server reads and answers, and the client reads the answer. The bytes of each wire are fed in
	// chunks, and the receiver runs after each chunk.
	auto chunkOf = [&](std::size_t left) -> std::size_t {
		if (byByte) {
			return 1;
		}

		return std::min<std::size_t>(left, 1 + random() % 7);
	};

	auto pump = [&]() {
		client.Process(0);

		std::size_t position = 0;

		while (position < toServer.bytes.size()) {
			std::size_t chunk = chunkOf(toServer.bytes.size() - position);

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
			std::size_t chunk = chunkOf(toClient.bytes.size() - position);

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

	// Each request is queued, then pumped until its reply has been handled. One pump writes the request, reads the answer and runs the
	// handler, so the second pump only checks that nothing else is left on either wire.
	auto step = [&]<typename Call>(Call call) {
		AtStatus status = call();

		while (status.status == ResultStatus::busy) {
			pump();
			status = call();
		}

		pump();
		pump();
	};

	step([&]() {
		return client.Write<"+JOIN", &OnJoin>(std::string_view("net"), std::optional<std::string_view>(std::string_view("pw")));
	});
	step([&]() {
		return client.Read<"+JOINED", &OnJoined>();
	});
	step([&]() {
		return client.Write<"+JOIN", &OnJoin>(std::string_view("open"), std::optional<std::string_view>());
	});
	step([&]() {
		return client.Read<"+JOINED", &OnJoined>();
	});
	step([&]() {
		return client.Read<"+TEMP", &OnTemp>();
	});
	step([&]() {
		return client.Run<"+STAT", &OnStat>();
	});
	step([&]() {
		return client.Run<"+CIFSR", &OnIp>();
	});
	step([&]() {
		return client.Run<"+GMR", &OnVersion>();
	});
	step([&]() {
		return client.Rows<"+LIST", &OnList>();
	});
	step([&]() {
		return client.RowsVisit<"+LIST", &OnRow, &OnRowsDone>();
	});
	step([&]() {
		return client.ReadRows<"+SCAN", &OnScan>();
	});
	step([&]() {
		return client.ReadRowsVisit<"+SCAN", &OnRow, &OnRowsDone>();
	});
	step([&]() {
		return client.Run<"+RUN", &OnRun>();
	});

	Outcome outcome;
	outcome.log = handled;
	outcome.clientWire = clientWire;
	outcome.serverWire = serverWire;
	return outcome;
}


// The log the session must produce, written from the design: a record and a raw value, the rows of eight with five kept and three
// skipped, the visit of all eight, and the two-row read with a visit.
const char* ExpectedLog =
	"join ok\n"
	"joined net -40 pw\n"
	"join ok\n"
	"joined open -40 absent\n"
	"temp 23\n"
	"stat 3 hello world\n"
	"ip 192.168.1.9\n"
	"version 2.2.0.0(Jul 1 2022)\n"
	"list 5 3 n0|n1|a),b|n3|n4\n"
	"row n0 -40\n"
	"row n1 -41\n"
	"row a),b -42\n"
	"row n3 -43\n"
	"row n4 -44\n"
	"row n5 -45\n"
	"row n6 -46\n"
	"row n7 -47\n"
	"rows done ok\n"
	"scan 2 0 n0|n1\n"
	"row n0 -40\n"
	"row n1 -41\n"
	"rows done ok\n"
	"run ok\n";


// The module's text, which the server writes for these requests: a record with an optional field, a status with its raw field in
// angle brackets, the CIFSR line with its comma, the raw version, and a row of the list written with its quoted ")" and ",".
void TestWireText(const Outcome& outcome) {
	AT_EQ(outcome.serverWire.find("+JOINED: \"net\",-40,\"pw\"\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("+JOINED: \"open\",-40\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("+STAT: <3,hello world>\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("+CIFSR:STAIP,\"192.168.1.9\"\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("AT version: 2.2.0.0(Jul 1 2022)\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("+LIST: (2,\"a),b\",-42,\"aa:bb\",3)\r\n") != std::string::npos, true);
	AT_EQ(outcome.serverWire.find("+SCAN: (0,\"n0\",-40,\"aa:bb\",1)\r\n") != std::string::npos, true);
}


void TestSessionsAgree() {
	Outcome byByte = RunSession(true, 0);
	AT_TEXT(byByte.log, ExpectedLog);
	TestWireText(byByte);

	const unsigned seeds[] = {1, 7, 2024, 99991};

	for (unsigned seed : seeds) {
		Outcome chunked = RunSession(false, seed);
		AT_TEXT(chunked.log, byByte.log);
		AT_TEXT(chunked.clientWire, byByte.clientWire);
		AT_TEXT(chunked.serverWire, byByte.serverWire);
	}
}


// A RawString is written as it is, up to the first CR or LF, so that a line is not split by its own text.
void TestRawStringWriter() {
	Wire wire;
	RawString<8> text = ToRaw<8>("ab\rcd");
	AtWriteValue(wire, text);
	AT_TEXT(wire.bytes, "ab");

	Wire tail;
	RawString<8> whole = ToRaw<8>("xyz");
	AtWriteValue(tail, whole);
	AT_TEXT(tail.bytes, "xyz");
}


}


int main() {
	TestSessionsAgree();
	TestRawStringWriter();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtReplies: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtReplies: all %d checks passed\n", checkCount);
	return 0;
}
