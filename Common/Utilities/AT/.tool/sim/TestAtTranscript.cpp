#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
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

// Transcript tests. A transcript is a modem session written by hand, in the format of the design: "> " is a line the client writes, "< "
// is a line the module sends, "# " is a comment, and "@ " moves the time to that many milliseconds. Each transcript is run in three ways:
// the whole module stream in chunks of up to 60 bytes (the ring holds 64), with one cut at every byte position, and one byte at a time.
// Every run must give the handler log and the written bytes that the test writes here from the design. A time record runs the client
// at the old time, then at the new one, so the bytes before it are read at the time they came.
//
// Raw bytes have their own records, as hex with no spaces: "= " is a run the module sends, such as a Data() URC's header and its data, and
// "> = " is a run the client writes, such as the data of a SendData request. A "<" alone is an empty module line, and it is the CRLF.


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
			"TestAtTranscript.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
			line,
			expression,
			Visible(actual).c_str(),
			Visible(expected).c_str()
		);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)


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
		case ResultStatus::invalidData:
			return "invalidData";
		case ResultStatus::invalidParameter:
			return "invalidParameter";
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


// The description of the transcripts: echo on, verbose CME errors with one text, and every reply the transcripts use.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().Echo().CmeErrors(AtCmeMode::verbose).CmeText<3, "operation not allowed">())
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+CWMODE">().Test().Read<int8>("mode")
		.Command<"+SCAN">().Run()
		.Command<"+DIAL">().Run().Failures("FAIL")
		.Command<"+SEND">().Run().Finals("SEND OK").Failures("SEND FAIL")
		.Command<"+RUN">().Run()
		.Urc<"WIFI GOT IP">()
		.Urc<"+IPD">(Argument<uint16>("length"), Argument<String<8>>("tag"), Optional<bool>("flag"));
}>;


void OnTemp(Result<int16, AtStatus> result) {
	Note(result.IsOk() ? "temp " + std::to_string(result.Value()) : "temp " + Describe(result.Error()));
}


void OnMode(Result<int8, AtStatus> result) {
	Note(result.IsOk() ? "mode " + std::to_string(result.Value()) : "mode " + Describe(result.Error()));
}


void OnLed(AtStatus status) {
	Note("led " + Describe(status));
}


void OnTest(AtStatus status) {
	Note("test " + Describe(status));
}


void OnScan(AtStatus status) {
	Note("scan " + Describe(status));
}


void OnDial(AtStatus status) {
	Note("dial " + Describe(status));
}


void OnSend(AtStatus status) {
	Note("send " + Describe(status));
}


void OnRun(AtStatus status) {
	Note("run " + Describe(status));
}


void OnGotIp() {
	Note("got ip");
}


void OnIpd(uint16 length, std::string_view tag, std::optional<bool> flag) {
	std::string text = "ipd " + std::to_string(length) + " " + std::string(tag) + " ";
	text += flag.has_value() ? (*flag ? "true" : "false") : "absent";
	Note(text);
}


using Binding = AtClientBinding<
	AtUrcOn<"WIFI GOT IP", &OnGotIp>,
	AtUrcOn<"+IPD", &OnIpd>
>;


// Four requests may wait, so a transcript can start all of its requests at once.
using Client = AtClient<Set, Binding, AtQueue<4>, AtEcho>;


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
		char* end = nullptr;
		long value = std::strtol(pair.c_str(), &end, 16);

		if (end == nullptr || *end != '\0' || pair.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
			return false;
		}

		target += static_cast<char>(value);
	}

	return true;
}


// Reads a transcript file. A record with another prefix fails the test.
bool ReadTranscript(const std::string& name, Transcript& transcript) {
	std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "transcripts" / name;
	std::ifstream file(path);

	if (!file) {
		std::fprintf(stderr, "TestAtTranscript.cpp: cannot open %s\n", path.string().c_str());
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
				std::fprintf(stderr, "TestAtTranscript.cpp: %s: a raw client record is not hex: %s\n", name.c_str(), line.c_str());
				return false;
			}
		} else if (line.compare(0, 2, "= ") == 0) {
			if (!AppendHex(transcript.module, line.substr(2))) {
				std::fprintf(stderr, "TestAtTranscript.cpp: %s: a raw module record is not hex: %s\n", name.c_str(), line.c_str());
				return false;
			}
		} else if (line.compare(0, 2, "> ") == 0) {
			transcript.writes += line.substr(2) + "\r\n";
		} else if (line.compare(0, 2, "< ") == 0) {
			transcript.module += line.substr(2) + "\r\n";
		} else if (line == "<") {
			// An empty module line. An editor may drop the blank after the "<", so the bare mark is also an empty line.
			transcript.module += "\r\n";
		} else if (line.compare(0, 2, "@ ") == 0) {
			transcript.times.emplace_back(transcript.module.size(), static_cast<uint32>(std::stoul(line.substr(2))));
		} else {
			std::fprintf(stderr, "TestAtTranscript.cpp: %s: a record must start with \"> \", \"< \", \"@ \" or \"# \": %s\n", name.c_str(), line.c_str());
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
template<typename ClientType>
void CheckTranscript(const char* name, void (*setup)(ClientType&), const std::string& expectedLog) {
	Transcript transcript;

	if (!ReadTranscript(name, transcript)) {
		failureCount++;
		checkCount++;
		return;
	}

	Run whole = RunTranscript<ClientType>(transcript, setup, {});
	AT_TEXT(whole.log, expectedLog);
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


void SetupUrcBetweenInfo(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
}


void SetupScan(Client& client) {
	client.Run<"+SCAN", &OnScan>();
}


void SetupCmeAndError(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
	client.Read<"+TEMP", &OnTemp>();
	client.Write<"+LED", &OnLed>(true);
}


void SetupFailureAndFinals(Client& client) {
	client.Run<"+DIAL", &OnDial>();
	client.Run<"+SEND", &OnSend>();
	client.Run<"+SEND", &OnSend>();
	client.Run<"+DIAL", &OnDial>();
}


void SetupTempAndRun(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
	client.Run<"+RUN", &OnRun>();
}


void SetupTemp(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
}


void SetupEchoAndUrc(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
	client.Write<"+LED", &OnLed>(false);
}


void SetupInvalidInfo(Client& client) {
	client.Read<"+TEMP", &OnTemp>();
	client.Read<"+CWMODE", &OnMode>();
	client.Test<"+CWMODE", &OnTest>();
}


// The module's answer to a chain. The server runs AT+LED=1;+DENY, and the second command fails. The server writes its echo and one
// final line. The client, which asked for AT+LED=1, must read that one final line as its status. A chained command has no AT prefix.
using ServerSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Dialect(AtDialect().Echo().CmeErrors(AtCmeMode::verbose).CmeText<3, "operation not allowed">())
		.Command<"+LED">().Write(Argument<bool>("on"))
		.Command<"+DENY">().Run();
}>;


struct ServerApp {
};


AtStatus ServerLed(AtCall<ServerApp> call, bool on) {
	static_cast<void>(call);
	static_cast<void>(on);
	return AtStatus{};
}


AtStatus ServerDeny(AtCall<ServerApp> call) {
	static_cast<void>(call);
	return AtCmeError(3);
}


using ServerBinding = AtServerBinding<
	AtOn<"+LED", &ServerLed>,
	AtOn<"+DENY", &ServerDeny>
>;


void TestChainFinalIsOneStatus() {
	ServerApp app;
	Wire fromServer;
	AtServer<ServerSet, ServerApp, ServerBinding> server(app, fromServer);

	const std::string chain = "AT+LED=1;+DENY\r\n";

	for (char character : chain) {
		server.Feed(static_cast<uint8>(character));
	}

	server.Process();

	// The echo of the line, its CR, then the one final line with its framing, then the echo of the LF of the CRLF pair.
	AT_TEXT(fromServer.bytes, "AT+LED=1;+DENY\r\r\n+CME ERROR: operation not allowed\r\n\n");

	Transcript transcript;
	transcript.writes = "AT+LED=1\r\n";
	transcript.module = fromServer.bytes;

	auto setup = [](Client& client) {
		client.Write<"+LED", &OnLed>(true);
	};

	Run whole = RunTranscript<Client>(transcript, setup, {});
	AT_TEXT(whole.log, "led error cme 3\n");
	AT_TEXT(whole.writes, transcript.writes);

	for (std::size_t position = 1; position < transcript.module.size(); position++) {
		Run split = RunTranscript<Client>(transcript, setup, {position});
		AT_TEXT(split.log, whole.log);
	}
}


// The data transcripts. The description has one prompt command, AT+CIPSEND, with its final SEND OK and its failure SEND FAIL, one read for
// a reply that a data URC can interrupt, and the +IPD URC with its length and Data(). The echo is on, as in the other transcripts.
using DataSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().Echo())
		.Modules(AtDataMode)
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK").Failures("SEND FAIL")
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;


// The handler of a Data() URC: the length, then each chunk with its offset, its bytes as text (CR and LF shown), and whether it is last.
void OnIpdData(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	std::string text;

	for (uint8 byte : chunk) {
		text += static_cast<char>(byte);
	}

	Note("ipd " + std::to_string(length) + " at " + std::to_string(offset) + " [" + Visible(text) + "]" + (last ? " last" : " more"));
}


using DataBinding = AtClientBinding<
	AtUrcOn<"+IPD", &OnIpdData>
>;


using DataClient = AtClient<DataSet, DataBinding, AtQueue<2>, AtEcho>;


// The five bytes of the SendData tests. They are static, so the span stays valid for the whole run.
constexpr uint8 kHello[] = {'h', 'e', 'l', 'l', 'o'};


void SetupSendHello(DataClient& client) {
	client.SendData<"+CIPSEND", &OnSend>(uint16{5}, std::span<const uint8>(kHello));
}


void SetupDataTemp(DataClient& client) {
	client.Read<"+TEMP", &OnTemp>();
}


void SetupNothingData(DataClient& client) {
	static_cast<void>(client);
}


// The failure code transcripts. The description has an echo, one command with a read form and a write form and a failure code line, and
// the URC WIFI GOT IP, which a transcript puts between a code line and its final. The write is AT+CWJAP=2 and the read is AT+CWJAP?.
using FailureSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().Echo())
		.Command<"+CWJAP">().Read<uint8>("channel").Write(Argument<uint8>("code")).FailureCode("+CWJAP")
		.Urc<"WIFI GOT IP">();
}>;


void OnCwjapWrite(AtStatus status) {
	Note("join " + Describe(status));
}


void OnCwjapRead(Result<uint8, AtStatus> result) {
	Note(result.IsOk() ? "read " + std::to_string(result.Value()) : "read " + Describe(result.Error()));
}


using FailureBinding = AtClientBinding<
	AtUrcOn<"WIFI GOT IP", &OnGotIp>
>;


using FailureClient = AtClient<FailureSet, FailureBinding, AtQueue<2>, AtEcho>;


void SetupJoinWrite(FailureClient& client) {
	client.Write<"+CWJAP", &OnCwjapWrite>(uint8{2});
}


void SetupChannelRead(FailureClient& client) {
	client.Read<"+CWJAP", &OnCwjapRead>();
}


}


int main() {
	CheckTranscript("urc_between_info_lines.txt", SetupUrcBetweenInfo, "got ip\ntemp 23\n");
	CheckTranscript("scan_urc_between_info_lines.txt", SetupScan, "got ip\nscan ok\n");
	CheckTranscript("cme_and_error.txt", SetupCmeAndError, "temp error cme 3\ntemp error\nled error cme 3\n");
	CheckTranscript("failure_and_declared_finals.txt", SetupFailureAndFinals, "dial error\nsend ok\nsend error\ndial ok\n");
	CheckTranscript("late_final_during_drain.txt", SetupTempAndRun, "temp timeout\nrun ok\n");
	CheckTranscript("late_ok_during_drain.txt", SetupTempAndRun, "temp timeout\nrun error\n");
	CheckTranscript("silent_module_drain.txt", SetupTempAndRun, "temp timeout\nrun ok\n");
	CheckTranscript("reply_one_ms_before_deadline.txt", SetupTemp, "temp 5\n");
	CheckTranscript("reply_at_deadline.txt", SetupTemp, "temp timeout\n");
	CheckTranscript("echo_and_urc_arguments.txt", SetupEchoAndUrc, "ipd 4 a,b true\ntemp 4\nled ok\n");
	CheckTranscript("invalid_info_and_unknown_lines.txt", SetupInvalidInfo, "temp invalidData\nmode 1\ntest ok\n");
	CheckTranscript("single_bad_then_good.txt", SetupTemp, "temp invalidData\n");
	CheckTranscript("single_good_then_bad.txt", SetupTemp, "temp invalidData\n");
	CheckTranscript("single_two_values.txt", SetupTemp, "temp invalidData\n");
	TestChainFinalIsOneStatus();

	CheckTranscript("send_data_urc_between_prompt_and_ok.txt", SetupSendHello, "ipd 3 at 0 [abc] last\nsend ok\n");
	CheckTranscript("send_data_fail.txt", SetupSendHello, "send error\n");
	CheckTranscript("send_error_before_prompt.txt", SetupSendHello, "send error\n");
	CheckTranscript("send_timeout_during_prompt_wait.txt", SetupSendHello, "send timeout\n");
	CheckTranscript("ipd_cr_lf_in_data.txt", SetupSendHello, "ipd 4 at 0 [\\r\\n\\r\\n] last\nsend ok\n");
	CheckTranscript("ipd_length_zero.txt", SetupNothingData, "ipd 0 at 0 [] last\n");
	CheckTranscript("ipd_back_to_back.txt", SetupNothingData, "ipd 2 at 0 [ab] last\nipd 1 at 0 [c] last\n");
	CheckTranscript("ipd_between_info_lines.txt", SetupDataTemp, "ipd 4 at 0 [OK\\r\\n] last\ntemp 23\n");
	CheckTranscript(
		"ipd_chunks_of_32.txt",
		SetupNothingData,
		"ipd 40 at 0 [01234567890123456789012345678901] more\nipd 40 at 32 [23456789] last\n");
	CheckTranscript("ipd_length_overflow.txt", SetupDataTemp, "temp 9\n");
	CheckTranscript("gt_outside_prompt.txt", SetupDataTemp, "temp 1\n");
	CheckTranscript("failure_code_error.txt", SetupJoinWrite, "join error failure 2\n");
	CheckTranscript("failure_code_blank.txt", SetupJoinWrite, "join error failure 3\n");
	CheckTranscript("failure_code_then_ok.txt", SetupJoinWrite, "join ok\n");
	CheckTranscript("failure_code_no_integer.txt", SetupJoinWrite, "join error\n");
	CheckTranscript("failure_code_too_big.txt", SetupJoinWrite, "join error\n");
	CheckTranscript("failure_code_max.txt", SetupJoinWrite, "join error failure 255\n");
	CheckTranscript("failure_code_urc_between.txt", SetupJoinWrite, "got ip\njoin error failure 4\n");
	CheckTranscript("failure_code_read_record.txt", SetupChannelRead, "read 6\n");
	CheckTranscript("failure_code_read_reply.txt", SetupChannelRead, "read 2\n");

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtTranscript: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtTranscript: all %d checks passed\n", checkCount);
	return 0;
}
