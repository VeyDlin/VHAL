#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Transcript tests for the reply forms. A transcript is a modem session written by hand, in the format of TestAtTranscript.cpp: "> " is a
// line the client writes, "< " is a line the module sends, "# " is a comment, and "@ " moves the time to that many milliseconds. Each one is
// run whole, cut at every byte position, and one byte at a time, and every run must give the handler log that the test writes here.


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
			"TestAtReplyTranscript.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
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


template<std::size_t Capacity>
String<Capacity> ToString(std::string_view text) {
	String<Capacity> value;

	for (char character : text) {
		value.characters[value.length] = character;
		value.length++;
	}

	return value;
}


// An access point of a list: its encryption, its name, its signal, and an optional channel. The row ends with the parentheses.
struct AccessPoint {
	int8 ecn = 0;
	String<32> ssid{};
	int8 rssi = 0;
	std::optional<uint8> channel{};
};


constexpr auto AccessPointRow = AtRow<AccessPoint>(
	AtField<&AccessPoint::ecn>("ecn"),
	AtField<&AccessPoint::ssid>("ssid"),
	AtField<&AccessPoint::rssi>("rssi"),
	AtField<&AccessPoint::channel>("channel")
).Open("(").Close(")");


// The description of the transcripts: echo on, and the reply forms. The rows are read into a list of two.
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Dialect(AtDialect().Echo())
		.Command<"+CIFSR">().Run<String<15>>("ip").Reply("+CIFSR:STAIP")
		.Command<"+GMR">().Run<RawString<64>>("version").Reply("AT version:")
		.Command<"+CWLAP">().Rows(AccessPointRow)
		.Command<"+CWLIST">().ReadRows(AccessPointRow)
		.Urc<"WIFI GOT IP">();
}>;


void OnIp(Result<String<15>, AtStatus> result) {
	Note(result.IsOk() ? "ip " + std::string(result.Value().View()) : "ip " + Describe(result.Error()));
}


void OnVersion(Result<RawString<64>, AtStatus> result) {
	Note(result.IsOk() ? "version " + std::string(result.Value().View()) : "version " + Describe(result.Error()));
}


// The rows of a list: the names, and the channel after a slash when the row has one. A list with an error is named by its status.
void OnList(const Result<AtRowList<AccessPoint, 2>, AtStatus>& result) {
	if (result.IsErr()) {
		Note("list " + Describe(result.Error()));
		return;
	}

	const AtRowList<AccessPoint, 2>& list = result.Value();
	std::string names;

	for (const AccessPoint& point : list) {
		if (!names.empty()) {
			names += "|";
		}

		names += std::string(point.ssid.View());

		if (point.channel.has_value()) {
			names += "/" + std::to_string(*point.channel);
		}
	}

	Note("list " + std::to_string(list.Count()) + " " + std::to_string(list.Skipped()) + " " + names);
}


void OnWList(const Result<AtRowList<AccessPoint, 2>, AtStatus>& result) {
	if (result.IsErr()) {
		Note("wlist " + Describe(result.Error()));
		return;
	}

	Note("wlist " + std::to_string(result.Value().Count()));
}


void OnGotIp() {
	Note("got ip");
}


using Binding = AtClientBinding<AtUrcOn<"WIFI GOT IP", &OnGotIp>>;


// Two rows of the list fit. A third is counted as skipped, so the list keeps the first two.
using Client = AtClient<Set, Binding, AtQueue<2>, AtEcho, AtRows<2>>;


// A transcript: its expected writes, the module's bytes, and the times at which the module records move the clock.
struct Transcript {
	std::string writes;
	std::string module;
	std::vector<std::pair<std::size_t, uint32>> times;
};


// Reads a transcript file. A record with another prefix fails the test.
bool ReadTranscript(const std::string& name, Transcript& transcript) {
	std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "transcripts" / name;
	std::ifstream file(path);

	if (!file) {
		std::fprintf(stderr, "TestAtReplyTranscript.cpp: cannot open %s\n", path.string().c_str());
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

		if (line.compare(0, 2, "> ") == 0) {
			transcript.writes += line.substr(2) + "\r\n";
		} else if (line.compare(0, 2, "< ") == 0) {
			transcript.module += line.substr(2) + "\r\n";
		} else if (line == "<") {
			transcript.module += "\r\n";
		} else if (line.compare(0, 2, "@ ") == 0) {
			transcript.times.emplace_back(transcript.module.size(), static_cast<uint32>(std::stoul(line.substr(2))));
		} else {
			std::fprintf(stderr, "TestAtReplyTranscript.cpp: %s: a record must start with \"> \", \"< \", \"@ \" or \"# \": %s\n", name.c_str(), line.c_str());
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


// Runs the transcript once. The client is fed in chunks of up to 60 bytes, and at each position in cuts it runs before the byte there.
template<typename Setup>
Run RunTranscript(const Transcript& transcript, Setup setup, const std::vector<std::size_t>& cuts) {
	handled.clear();

	Wire wire;
	Client client(wire);
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
template<typename Setup>
void CheckTranscript(const char* name, Setup setup, const std::string& expectedLog) {
	Transcript transcript;

	if (!ReadTranscript(name, transcript)) {
		failureCount++;
		checkCount++;
		return;
	}

	Run whole = RunTranscript(transcript, setup, {});
	AT_TEXT(whole.log, expectedLog);
	AT_TEXT(whole.writes, transcript.writes);

	for (std::size_t position = 1; position < transcript.module.size(); position++) {
		Run split = RunTranscript(transcript, setup, {position});
		AT_TEXT(split.log, whole.log);
		AT_TEXT(split.writes, whole.writes);
	}

	std::vector<std::size_t> everyByte;

	for (std::size_t position = 1; position < transcript.module.size(); position++) {
		everyByte.push_back(position);
	}

	Run oneByte = RunTranscript(transcript, setup, everyByte);
	AT_TEXT(oneByte.log, whole.log);
	AT_TEXT(oneByte.writes, whole.writes);
}


void SetupCifsr(Client& client) {
	client.Run<"+CIFSR", &OnIp>();
}


void SetupGmr(Client& client) {
	client.Run<"+GMR", &OnVersion>();
}


void SetupRows(Client& client) {
	client.Rows<"+CWLAP", &OnList>();
}


void SetupReadRows(Client& client) {
	client.ReadRows<"+CWLIST", &OnWList>();
}


}


int main() {
	CheckTranscript("cifsr_stap.txt", SetupCifsr, "ip 192.168.1.9\n");
	CheckTranscript("cifsr_ipv6.txt", SetupCifsr, "ip 192.168.1.9\n");
	CheckTranscript("gmr_raw.txt", SetupGmr, "version 2.2.0.0(Jul 1 2022 10:34:36)\n");
	CheckTranscript("rows_urc_between.txt", SetupRows, "got ip\nlist 2 1 home|a),b/6\n");
	CheckTranscript("rows_bad_row.txt", SetupReadRows, "wlist invalidData\n");

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtReplyTranscript: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtReplyTranscript: all %d checks passed\n", checkCount);
	return 0;
}
