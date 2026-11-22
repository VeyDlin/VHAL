#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
#include <Utilities/AT/Descriptions/EspAt.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Tests of the ESP-AT description (Descriptions/EspAt.h). The client is built with AtEcho, AtRows<8> and AtAwait, and the server with
// the description's commands, so both must compile: every form the description declares is bound, except the prompt of AT+CIPSEND, which
// the server answers with ERROR. The transcripts are written by hand from the ESP-AT command reference extract (source commit b2921f8f).
// Each one is a module session: "> " is a line the client writes, "< " a line the module sends, "= " raw module bytes in hex, "> = " raw
// client bytes in hex, "@ " moves the time to that many milliseconds, and "# " is a comment. Each transcript is run whole, cut at every
// byte position, and one byte at a time, and every run must give the handler log and the written bytes of the whole run.


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
			"TestAtEspAt.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
			line,
			expression,
			Visible(actual).c_str(),
			Visible(expected).c_str()
		);
	}
}


void ExpectTrue(bool condition, const char* expression, int line) {
	checkCount++;

	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAtEspAt.cpp:%d: %s is false\n", line, expression);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_TRUE(condition) ExpectTrue(condition, #condition, __LINE__)


// The handler log, one line for each handler call, in the order the calls are made.
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
		case ResultStatus::invalidData:
			return "invalidData";
		case ResultStatus::invalidParameter:
			return "invalidParameter";
		default:
			return "other";
	}
}


// The output of a side, in the order it is written.
struct Wire : public Print {
	std::string bytes;

	void WriteRaw(const char* data, size_t size) override {
		bytes.append(data, size);
	}
};


// The URC handlers of the client. A URC with no argument is a plain call. The +IPD handler gets each chunk of its data.
void OnReady() {
	Note("ready");
}


void OnBusy() {
	Note("busy");
}


void OnWifiConnected() {
	Note("wifi connected");
}


void OnWifiGotIp() {
	Note("wifi got ip");
}


void OnWifiDisconnect() {
	Note("wifi disconnect");
}


void OnConnect() {
	Note("connect");
}


void OnClosed() {
	Note("closed");
}


void OnIpd(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	std::string text;

	for (uint8 byte : chunk) {
		text += static_cast<char>(byte);
	}

	Note("ipd " + std::to_string(length) + " at " + std::to_string(offset) + " [" + Visible(text) + "]" + (last ? " last" : " more"));
}


// The reply handlers of the client. Each one writes its outcome to the log.
void OnEcho(AtStatus status) {
	Note("echo " + Describe(status));
}


void OnGmr(Result<RawString<64>, AtStatus> result) {
	Note(result.IsOk() ? "version " + std::string(result.Value().View()) : "version " + Describe(result.Error()));
}


void OnMode(Result<uint8, AtStatus> result) {
	Note(result.IsOk() ? "mode " + std::to_string(result.Value()) : "mode " + Describe(result.Error()));
}


void OnModeWrite(AtStatus status) {
	Note("cwmode write " + Describe(status));
}


void OnJoined(Result<EspAtJoinedNetwork, AtStatus> result) {
	if (!result.IsOk()) {
		Note("joined " + Describe(result.Error()));
		return;
	}

	const EspAtJoinedNetwork& network = result.Value();
	Note(
		"joined " + std::string(network.ssid.View()) + " " + std::string(network.bssid.View()) + " " + std::to_string(network.channel) + " " +
		std::to_string(network.rssi));
}


void OnJoinWrite(AtStatus status) {
	Note("join " + Describe(status));
}


void OnQuit(AtStatus status) {
	Note("quit " + Describe(status));
}


void OnScan(const Result<AtRowList<EspAtAccessPoint, 8>, AtStatus>& result) {
	if (!result.IsOk()) {
		Note("scan " + Describe(result.Error()));
		return;
	}

	const AtRowList<EspAtAccessPoint, 8>& list = result.Value();
	Note("scan " + std::to_string(list.Count()) + " skipped " + std::to_string(list.Skipped()));

	for (const EspAtAccessPoint& row : list) {
		Note(
			"row " + std::to_string(row.ecn) + " " + std::string(row.ssid.View()) + " " + std::to_string(row.rssi) + " " +
			std::string(row.mac.View()) + " " + std::to_string(row.channel));
	}
}


void OnAddress(Result<String<15>, AtStatus> result) {
	Note(result.IsOk() ? "address " + std::string(result.Value().View()) : "address " + Describe(result.Error()));
}


void OnMux(Result<uint8, AtStatus> result) {
	Note(result.IsOk() ? "mux " + std::to_string(result.Value()) : "mux " + Describe(result.Error()));
}


void OnStart(AtStatus status) {
	Note("start " + Describe(status));
}


void OnSend(AtStatus status) {
	Note("send " + Describe(status));
}


void OnClose(AtStatus status) {
	Note("close " + Describe(status));
}


// The client binding: the URC handlers of the ESP-AT URCs, each one by its prefix.
using EspAtBinding = AtClientBinding<
	AtUrcOn<"ready", &OnReady>,
	AtUrcOn<"busy p...", &OnBusy>,
	AtUrcOn<"WIFI CONNECTED", &OnWifiConnected>,
	AtUrcOn<"WIFI GOT IP", &OnWifiGotIp>,
	AtUrcOn<"WIFI DISCONNECT", &OnWifiDisconnect>,
	AtUrcOn<"CONNECT", &OnConnect>,
	AtUrcOn<"CLOSED", &OnClosed>,
	AtUrcOn<"+IPD", &OnIpd>
>;


// The client with every module the description needs: echo, rows, and Await. Four requests may wait, so a transcript can queue them all.
using EspAtClient = AtClient<EspAt, EspAtBinding, AtQueue<4>, AtEcho, AtRows<8>, AtAwait>;


// The Await calls, compiled but never run here: the Await tests run them with a scripted module. A call of each form is needed for the
// instantiation of its code, so a form that does not compile fails this file.
[[maybe_unused]] void EspAtAwaitForms(EspAtClient& client) {
	static_cast<void>(client.AwaitRead<"+CWMODE">());
	static_cast<void>(client.AwaitRun<"+GMR">());
	static_cast<void>(client.AwaitRun<"+CWQAP">());
	static_cast<void>(client.AwaitWrite<"+CWMODE">(uint8{1}, std::optional<bool>()));
	static_cast<void>(client.AwaitValue<"E">(uint8{0}));
	static_cast<void>(client.AwaitRows<"+CWLAP">());
	constexpr uint8 hello[] = {'h'};

	static_cast<void>(client.AwaitSendData<"+CIPSEND">(uint16{1}, std::span<const uint8>(hello)));
}


// A transcript: its expected writes, the module's bytes, and the times at which the module records move the clock.
struct Transcript {
	std::string writes;
	std::string module;
	std::vector<std::pair<std::size_t, uint32>> times;
};


// Appends the bytes of a hex text, two digits to a byte. Returns false for a text that is not an even run of hex digits.
bool AppendHex(std::string& target, const std::string& hex) {
	if (hex.size() % 2 != 0) {
		return false;
	}

	for (std::size_t index = 0; index < hex.size(); index += 2) {
		std::string pair = hex.substr(index, 2);

		if (pair.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			return false;
		}

		target += static_cast<char>(std::strtol(pair.c_str(), nullptr, 16));
	}

	return true;
}


// Reads a transcript file. A record with another prefix fails the test.
bool ReadTranscript(const std::string& name, Transcript& transcript) {
	std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "transcripts" / name;
	std::ifstream file(path);

	if (!file) {
		std::fprintf(stderr, "TestAtEspAt.cpp: cannot open %s\n", path.string().c_str());
		return false;
	}

	std::string line;

	while (std::getline(file, line)) {
		if (!line.empty() && line.back() == '\r') {
			line.pop_back();
		}

		if (line.empty() || line[0] == '#') {
			continue;
		}

		if (line.compare(0, 4, "> = ") == 0) {
			if (!AppendHex(transcript.writes, line.substr(4))) {
				std::fprintf(stderr, "TestAtEspAt.cpp: %s: a raw client record is not hex: %s\n", name.c_str(), line.c_str());
				return false;
			}
		} else if (line.compare(0, 2, "= ") == 0) {
			if (!AppendHex(transcript.module, line.substr(2))) {
				std::fprintf(stderr, "TestAtEspAt.cpp: %s: a raw module record is not hex: %s\n", name.c_str(), line.c_str());
				return false;
			}
		} else if (line.compare(0, 2, "> ") == 0) {
			transcript.writes += line.substr(2) + "\r\n";
		} else if (line.compare(0, 2, "< ") == 0) {
			transcript.module += line.substr(2) + "\r\n";
		} else if (line == "<") {
			transcript.module += "\r\n";
		} else if (line.compare(0, 2, "@ ") == 0) {
			transcript.times.emplace_back(transcript.module.size(), static_cast<uint32>(std::stoul(line.substr(2))));
		} else {
			std::fprintf(stderr, "TestAtEspAt.cpp: %s: a record must start with \"> \", \"< \", \"= \", \"@ \" or \"# \": %s\n", name.c_str(), line.c_str());
			return false;
		}
	}

	return true;
}


// The result of one run: the handler log, and the bytes the client wrote.
struct Run {
	std::string log;
	std::string writes;
};


// Runs the transcript once. cuts lists the byte positions where the client runs before the byte there; the ring is run every 60 bytes
// as well. A time record runs the client at the old time, then at the new one.
template<typename ClientType, typename Setup>
Run RunTranscript(const Transcript& transcript, Setup setup, const std::vector<std::size_t>& cuts) {
	handled.clear();

	Wire wire;
	ClientType client(wire);
	setup(client);

	uint32 now = 0;
	client.Process(now);

	std::size_t eventIndex = 0;

	for (std::size_t position = 0; position <= transcript.module.size(); position++) {
		while (eventIndex < transcript.times.size() && transcript.times[eventIndex].first == position) {
			client.Process(now);
			now = transcript.times[eventIndex].second;
			client.Process(now);
			eventIndex++;
		}

		if (position == transcript.module.size()) {
			break;
		}

		bool cut = position % 60 == 0 && position > 0;

		for (std::size_t point : cuts) {
			cut = cut || point == position;
		}

		if (cut) {
			client.Process(now);
		}

		client.Feed(static_cast<uint8>(transcript.module[position]));
	}

	client.Process(now);

	Run run;
	run.log = handled;
	run.writes = wire.bytes;
	return run;
}


// Runs a transcript whole, cut at every byte position, and one byte at a time, and checks each run against the expected log and writes.
template<typename ClientType, typename Setup>
void CheckTranscript(const char* name, Setup setup, const std::string& expectedLog, const std::string& expectedWrites) {
	Transcript transcript;

	if (!ReadTranscript(name, transcript)) {
		failureCount++;
		checkCount++;
		return;
	}

	Run whole = RunTranscript<ClientType>(transcript, setup, {});
	AT_TEXT(whole.log, expectedLog);
	AT_TEXT(whole.writes, expectedWrites);
	AT_TEXT(whole.writes, transcript.writes);

	for (std::size_t position = 1; position < transcript.module.size(); position++) {
		Run split = RunTranscript<ClientType>(transcript, setup, {position});
		AT_TEXT(split.log, whole.log);
		AT_TEXT(split.writes, whole.writes);
	}

	std::vector<std::size_t> everyByte;

	for (std::size_t position = 1; position < transcript.module.size(); position++) {
		everyByte.push_back(position);
	}

	Run oneByte = RunTranscript<ClientType>(transcript, setup, everyByte);
	AT_TEXT(oneByte.log, whole.log);
	AT_TEXT(oneByte.writes, whole.writes);
}


// The five bytes of the SendData test. They are static, so the span stays valid for the whole run.
constexpr uint8 kHello[] = {'h', 'e', 'l', 'l', 'o'};


void SetupNothing(EspAtClient& client) {
	static_cast<void>(client);
}


void SetupEcho0(EspAtClient& client) {
	client.Value<"E", &OnEcho>(uint8{0});
}


void SetupGmr(EspAtClient& client) {
	client.Run<"+GMR", &OnGmr>();
}


void SetupCwmode(EspAtClient& client) {
	client.Read<"+CWMODE", &OnMode>();
	client.Write<"+CWMODE", &OnModeWrite>(uint8{1}, std::optional<bool>());
}


void SetupBusy(EspAtClient& client) {
	client.Read<"+CWMODE", &OnMode>();
}


void SetupJoin(EspAtClient& client) {
	client.Write<"+CWJAP", &OnJoinWrite>(std::string_view("net"), std::string_view("secret"), std::optional<std::string_view>());
}


void SetupJoinBad(EspAtClient& client) {
	client.Write<"+CWJAP", &OnJoinWrite>(std::string_view("net"), std::string_view("bad"), std::optional<std::string_view>());
}


void SetupJoinQuery(EspAtClient& client) {
	client.Read<"+CWJAP", &OnJoined>();
}


void SetupScan(EspAtClient& client) {
	client.Rows<"+CWLAP", &OnScan>();
}


void SetupAddress(EspAtClient& client) {
	client.Run<"+CIFSR", &OnAddress>();
}


void SetupMux(EspAtClient& client) {
	client.Read<"+CIPMUX", &OnMux>();
}


void SetupStart(EspAtClient& client) {
	client.Write<"+CIPSTART", &OnStart>(
		std::string_view("TCP"),
		std::string_view("192.0.2.1"),
		uint16{8000},
		std::optional<uint16>());
}


void SetupSendHello(EspAtClient& client) {
	client.SendData<"+CIPSEND", &OnSend>(uint16{5}, std::span<const uint8>(kHello));
}


void SetupClose(EspAtClient& client) {
	client.Run<"+CIPCLOSE", &OnClose>();
}


void SetupTimeoutThenAt(EspAtClient& client) {
	client.Read<"+CWMODE", &OnMode>();
}


void SetupLateReply(EspAtClient& client) {
	client.Read<"+CWMODE", &OnMode>();
	client.Run<"+CWQAP", &OnQuit>();
}


// The server of the description, bound to every form the description declares except the prompt of AT+CIPSEND. The handlers are plain
// functions, and the application is a small struct that holds the mode and the multiplex flag.
struct EspAtServerApp {
	uint8 mode = 0;
	uint8 mux = 0;
};


// Fills a RawString with text of its capacity or less.
RawString<64> EspAtRawText(std::string_view text) {
	RawString<64> value;

	for (char character : text) {
		value.characters[value.length] = character;
		value.length++;
	}

	return value;
}


// Fills a String with text of its capacity or less.
String<15> EspAtText15(std::string_view text) {
	String<15> value;

	for (char character : text) {
		value.characters[value.length] = character;
		value.length++;
	}

	return value;
}


Result<RawString<64>, AtStatus> ServerGmr(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return EspAtRawText("2.2.0.0(test)");
}


AtStatus ServerRst(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return AtStatus{};
}


Result<uint8, AtStatus> ServerModeRead(AtCall<EspAtServerApp> call) {
	return call.app.mode;
}


AtStatus ServerModeWrite(AtCall<EspAtServerApp> call, uint8 mode, std::optional<bool> autoConnect) {
	static_cast<void>(autoConnect);
	call.app.mode = mode;
	return AtStatus{};
}


Result<EspAtJoinedNetwork, AtStatus> ServerJoinRead(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return EspAtJoinedNetwork{};
}


AtStatus ServerJoinWrite(AtCall<EspAtServerApp> call, std::string_view ssid, std::string_view password, std::optional<std::string_view> bssid) {
	static_cast<void>(call);
	static_cast<void>(ssid);
	static_cast<void>(password);
	static_cast<void>(bssid);
	return AtStatus{};
}


AtStatus ServerQuit(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return AtStatus{};
}


AtStatus ServerScan(AtCall<EspAtServerApp> call, AtRowWriter<EspAtAccessPoint>& out) {
	static_cast<void>(call);

	EspAtAccessPoint row;
	row.ecn = 3;
	row.rssi = -40;
	row.channel = 6;
	out.Add(row);
	return AtStatus{};
}


Result<String<15>, AtStatus> ServerAddress(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return EspAtText15("192.168.1.9");
}


Result<uint8, AtStatus> ServerMuxRead(AtCall<EspAtServerApp> call) {
	return call.app.mux;
}


AtStatus ServerMuxWrite(AtCall<EspAtServerApp> call, uint8 mode) {
	call.app.mux = mode;
	return AtStatus{};
}


AtStatus ServerStart(AtCall<EspAtServerApp> call, std::string_view type, std::string_view host, uint16 port, std::optional<uint16> keepAlive) {
	static_cast<void>(call);
	static_cast<void>(type);
	static_cast<void>(host);
	static_cast<void>(port);
	static_cast<void>(keepAlive);
	return AtStatus{};
}


AtStatus ServerClose(AtCall<EspAtServerApp> call) {
	static_cast<void>(call);
	return AtStatus{};
}


using EspAtServerBinding = AtServerBinding<
	AtOn<"+GMR", &ServerGmr>,
	AtOn<"+RST", &ServerRst>,
	AtOn<"+CWMODE", &ServerModeRead>,
	AtOn<"+CWMODE", &ServerModeWrite>,
	AtOn<"+CWJAP", &ServerJoinRead>,
	AtOn<"+CWJAP", &ServerJoinWrite>,
	AtOn<"+CWQAP", &ServerQuit>,
	AtOn<"+CWLAP", &ServerScan>,
	AtOn<"+CIFSR", &ServerAddress>,
	AtOn<"+CIPMUX", &ServerMuxRead>,
	AtOn<"+CIPMUX", &ServerMuxWrite>,
	AtOn<"+CIPSTART", &ServerStart>,
	AtOn<"+CIPCLOSE", &ServerClose>
>;


using EspAtServer = AtServer<EspAt, EspAtServerApp, EspAtServerBinding>;


// Feeds one line to the server, and runs it.
void ServerLine(EspAtServer& server, std::string_view line) {
	for (char character : line) {
		server.Feed(static_cast<uint8>(character));
	}

	server.Process();
}


// The description's server answers its forms, and writes the info line of a read, the URC queued by the application, and the ERROR of the
// prompt command, which has no handler. The check is on the text the replies must have, not on every byte of the echo.
void TestServerAnswers() {
	EspAtServerApp app;
	Wire fromServer;
	EspAtServer server(app, fromServer);

	ServerLine(server, "AT+CWMODE=2\r");
	ServerLine(server, "AT+CWMODE?\r");
	AT_TRUE(fromServer.bytes.find("+CWMODE: 2\r\n") != std::string::npos);

	fromServer.bytes.clear();
	ServerLine(server, "AT+CIPSEND=5\r");
	AT_TRUE(fromServer.bytes.find("ERROR\r\n") != std::string::npos);

	fromServer.bytes.clear();
	server.Urc<"WIFI GOT IP">();
	server.Process();
	AT_TRUE(fromServer.bytes.find("WIFI GOT IP\r\n") != std::string::npos);
}


}


int main() {
	CheckTranscript<EspAtClient>("espat_boot.txt", SetupNothing, "ready\nwifi disconnect\n", "");
	CheckTranscript<EspAtClient>("espat_ate0.txt", SetupEcho0, "echo ok\n", "ATE0\r\n");
	CheckTranscript<EspAtClient>("espat_gmr.txt", SetupGmr, "version 2.2.0.0(abc - ESP32 - Jul 19 2023)\n", "AT+GMR\r\n");
	CheckTranscript<EspAtClient>("espat_cwmode.txt", SetupCwmode, "mode 1\ncwmode write ok\n", "AT+CWMODE?\r\nAT+CWMODE=1\r\n");
	CheckTranscript<EspAtClient>("espat_busy.txt", SetupBusy, "busy\nmode 1\n", "AT+CWMODE?\r\n");
	CheckTranscript<EspAtClient>(
		"espat_cwjap_ok.txt",
		SetupJoin,
		"wifi connected\nwifi got ip\njoin ok\n",
		"AT+CWJAP=\"net\",\"secret\"\r\n");
	CheckTranscript<EspAtClient>("espat_cwjap_fail.txt", SetupJoinBad, "join error\n", "AT+CWJAP=\"net\",\"bad\"\r\n");
	CheckTranscript<EspAtClient>(
		"espat_cwjap_query.txt",
		SetupJoinQuery,
		"joined net aa:bb:cc:dd:ee:ff 6 -45\n",
		"AT+CWJAP?\r\n");
	CheckTranscript<EspAtClient>(
		"espat_cwlap.txt",
		SetupScan,
		"wifi disconnect\nscan 4 skipped 0\nrow 3 home -40 aa:bb:cc:dd:ee:01 6\nrow 4 my,net -72 aa:bb:cc:dd:ee:02 11\n"
		"row 0 cafe -88 aa:bb:cc:dd:ee:03 1\nrow 3 x -60 aa:bb:cc:dd:ee:04 3\n",
		"AT+CWLAP\r\n");
	CheckTranscript<EspAtClient>("espat_cwlap_bad_row.txt", SetupScan, "scan invalidData\n", "AT+CWLAP\r\n");
	CheckTranscript<EspAtClient>("espat_cifsr.txt", SetupAddress, "address 192.168.1.9\n", "AT+CIFSR\r\n");
	CheckTranscript<EspAtClient>("espat_cipmux.txt", SetupMux, "mux 0\n", "AT+CIPMUX?\r\n");
	CheckTranscript<EspAtClient>(
		"espat_cipstart.txt",
		SetupStart,
		"connect\nstart ok\n",
		"AT+CIPSTART=\"TCP\",\"192.0.2.1\",8000\r\n");
	CheckTranscript<EspAtClient>("espat_cipsend.txt", SetupSendHello, "send ok\n", "AT+CIPSEND=5\r\nhello");
	CheckTranscript<EspAtClient>(
		"espat_cipsend_ipd.txt",
		SetupSendHello,
		"ipd 3 at 0 [abc] last\nsend ok\n",
		"AT+CIPSEND=5\r\nhello");
	CheckTranscript<EspAtClient>("espat_cipclose.txt", SetupClose, "closed\nclose ok\n", "AT+CIPCLOSE\r\n");
	CheckTranscript<EspAtClient>("espat_timeout_then_at.txt", SetupTimeoutThenAt, "mode timeout\n", "AT+CWMODE?\r\nAT\r\n");
	CheckTranscript<EspAtClient>(
		"espat_late_reply_then_drain.txt",
		SetupLateReply,
		"mode timeout\nquit ok\n",
		"AT+CWMODE?\r\nAT\r\nAT+CWQAP\r\n");

	TestServerAnswers();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtEspAt: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtEspAt: all %d checks passed\n", checkCount);
	return 0;
}
