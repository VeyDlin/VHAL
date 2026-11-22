#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// Unit tests of the data mode: the prompt of a SendData request, the raw run of a Data() URC, the chunks of a run, and a server that answers
// a prompt command with ERROR. Each test feeds its bytes by hand and checks the handler log, the bytes the client wrote and the chunks.


namespace {


int failureCount = 0;
int checkCount = 0;


void ExpectText(const std::string& actual, const std::string& expected, const char* expression, int line) {
	checkCount++;

	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtData.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n", line, expression, actual.c_str(), expected.c_str());
	}
}


void ExpectTrue(bool condition, const char* expression, int line) {
	checkCount++;

	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestAtData.cpp:%d: %s is false\n", line, expression);
	}
}


#define AT_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)
#define AT_TRUE(condition) ExpectTrue((condition), #condition, __LINE__)


// The handler log of the client, one line for each call, and the bytes of the chunks in the order they came.
std::string handled;
std::string received;


void Note(const std::string& text) {
	handled += text;
	handled += "\n";
}


struct Wire : public Print {
	std::string bytes;

	void WriteRaw(const char* data, size_t size) override {
		bytes.append(data, size);
	}
};


// Feeds the bytes into the client, and runs it at time zero. The ring holds 64 bytes, so the client runs every 60 bytes, as the transcript
// driver does, and no byte is dropped.
template<typename ClientType>
void Feed(ClientType& client, std::string_view text) {
	std::size_t fed = 0;

	for (char character : text) {
		client.Feed(static_cast<uint8>(character));
		fed++;

		if (fed % 60 == 0) {
			client.Process(0);
		}
	}

	client.Process(0);
}


std::string Describe(AtStatus status) {
	switch (status.status) {
		case ResultStatus::ok:
			return "ok";
		case ResultStatus::error:
			return "error";
		case ResultStatus::timeout:
			return "timeout";
		case ResultStatus::busy:
			return "busy";
		default:
			return "other";
	}
}


// The description of the client: a read for a reply, a prompt command with its finals and its failure, and the +IPD URC with its length and
// Data().
using Set = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Modules(AtDataMode)
		.Command<"+TEMP">().Read<int16>("celsius")
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK").Failures("SEND FAIL")
		.Urc<"+IPD">(Argument<uint16>("length"), Data());
}>;


void OnTemp(Result<int16, AtStatus> result) {
	Note(result.IsOk() ? "temp " + std::to_string(result.Value()) : "temp " + Describe(result.Error()));
}


void OnSent(AtStatus status) {
	Note("sent " + Describe(status));
}


void OnIpd(uint16 length, uint16 offset, std::span<const uint8> chunk, bool last) {
	Note("ipd " + std::to_string(length) + " " + std::to_string(offset) + " " + std::to_string(chunk.size()) + (last ? " last" : " more"));

	for (uint8 byte : chunk) {
		received += static_cast<char>(byte);
	}
}


using Binding = AtClientBinding<
	AtUrcOn<"+IPD", &OnIpd>
>;


// A client with one request at a time, so a second request is refused with busy when the first is open.
using Client = AtClient<Set, Binding, AtQueue<2>>;


using SingleClient = AtClient<Set, Binding, AtQueue<1>>;


// Five and two data bytes, in static storage, so the spans stay valid while the tests run.
constexpr uint8 kHello[] = {'h', 'e', 'l', 'l', 'o'};
constexpr uint8 kTwo[] = {'x', 'y'};


// The request is written at once. Its data is written only after the prompt, and the request ends at SEND OK.
void TestSendWritesDataAfterPrompt() {
	handled.clear();
	Wire wire;
	Client client(wire);

	client.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kHello));
	client.Process(0);
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\n");

	Feed(client, "OK\r\n");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\n");
	AT_TEXT(handled, "");

	Feed(client, ">");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhello");

	Feed(client, " ");
	Feed(client, "Recv 5 bytes\r\n\r\nSEND OK\r\n");
	AT_TEXT(handled, "sent ok\n");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhello");
}


// A '>' that is not at the start of a line is an ordinary character. The request does not take the prompt, so no data is written.
void TestPromptNeedsLineStart() {
	handled.clear();
	Wire wire;
	Client client(wire);

	client.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kHello));
	client.Process(0);
	Feed(client, "OK\r\nx>\r\nERROR\r\n");

	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\n");
	AT_TEXT(handled, "sent error\n");
}


// A '>' outside a prompt request, here the reply of a read, is an ordinary line, and writes nothing.
void TestPromptIgnoredOutsidePromptRequest() {
	handled.clear();
	Wire wire;
	Client client(wire);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);
	Feed(client, "+TEMP: 4\r\n>\r\nOK\r\n");

	AT_TEXT(wire.bytes, "AT+TEMP?\r\n");
	AT_TEXT(handled, "temp 4\n");
}


// Two requests with data: the second is sent only after the first has ended, and its data follows its own prompt.
void TestSecondDataRequestWaitsForFirst() {
	handled.clear();
	Wire wire;
	Client client(wire);

	client.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kHello));
	client.SendData<"+CIPSEND", &OnSent>(uint16{2}, std::span<const uint8>(kTwo));
	client.Process(0);

	Feed(client, "OK\r\n>");
	Feed(client, "SEND OK\r\n");
	AT_TEXT(handled, "sent ok\n");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhelloAT+CIPSEND=2\r\n");

	Feed(client, "OK\r\n>");
	Feed(client, "SEND OK\r\n");
	AT_TEXT(handled, "sent ok\nsent ok\n");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhelloAT+CIPSEND=2\r\nxy");
}


// A full queue refuses a SendData call with busy, and nothing is queued for it.
void TestSendDataBusyWhenFull() {
	handled.clear();
	Wire wire;
	SingleClient client(wire);

	AtStatus first = client.SendData<"+CIPSEND", &OnSent>(uint16{5}, std::span<const uint8>(kHello));
	AtStatus second = client.SendData<"+CIPSEND", &OnSent>(uint16{2}, std::span<const uint8>(kTwo));
	AT_TEXT(Describe(first), "ok");
	AT_TEXT(Describe(second), "busy");

	client.Process(0);
	Feed(client, "OK\r\n>");
	Feed(client, "SEND OK\r\n");
	AT_TEXT(handled, "sent ok\n");
	AT_TEXT(wire.bytes, "AT+CIPSEND=5\r\nhello");
}


// A zero-length request writes no data after its prompt, and its final line ends it.
void TestSendZeroLength() {
	handled.clear();
	Wire wire;
	Client client(wire);

	client.SendData<"+CIPSEND", &OnSent>(uint16{0}, std::span<const uint8>());
	client.Process(0);
	Feed(client, "OK\r\n>");
	Feed(client, "SEND OK\r\n");

	AT_TEXT(wire.bytes, "AT+CIPSEND=0\r\n");
	AT_TEXT(handled, "sent ok\n");
}


// A run of 70 bytes with CR and LF inside it. It is delivered as 32, 32 and 6 bytes, at offsets 0, 32 and 64, and only the last is marked
// last. The bytes are the bytes that were sent.
void TestDataChunksOfRun() {
	handled.clear();
	received.clear();
	Wire wire;
	Client client(wire);

	std::string payload;

	for (int index = 0; index < 70; index++) {
		payload += static_cast<char>('A' + index % 26);
	}

	payload[10] = '\r';
	payload[11] = '\n';

	Feed(client, "+IPD,70:" + payload + "\r\n");

	AT_TEXT(handled, "ipd 70 0 32 more\nipd 70 32 32 more\nipd 70 64 6 last\n");
	AT_TEXT(received, payload);
}


// A run of length 0 is one call, with an empty chunk and last set.
void TestDataLengthZero() {
	handled.clear();
	received.clear();
	Wire wire;
	Client client(wire);

	Feed(client, "+IPD,0:\r\n");

	AT_TEXT(handled, "ipd 0 0 0 last\n");
	AT_TEXT(received, "");
}


// A length that does not fit its type is refused: the header is not a run, so no handler is called, and the bytes are read as a line.
void TestDataLengthOverflowRefused() {
	handled.clear();
	received.clear();
	Wire wire;
	Client client(wire);

	Feed(client, "+IPD,70000:abc\r\n");

	AT_TEXT(handled, "");
	AT_TEXT(received, "");
}


// A Data() URC during a read does not end the read, and the run is not read as lines.
void TestDataDuringReadKeepsRequest() {
	handled.clear();
	received.clear();
	Wire wire;
	Client client(wire);

	client.Read<"+TEMP", &OnTemp>();
	client.Process(0);

	// The run is five bytes, "OK", CR, LF and "E". The CRLF after it is the line end, and the read is still open.
	Feed(client, "+TEMP: 8\r\n+IPD,5:OK\r\nE\r\n");
	AT_TEXT(handled, "ipd 5 0 5 last\n");
	AT_TEXT(received, "OK\r\nE");

	Feed(client, "OK\r\n");
	AT_TEXT(handled, "ipd 5 0 5 last\ntemp 8\n");
}


// The server: a prompt command with no handler is answered with ERROR, and its queued URC is written after it. The set has a Data() URC,
// which the server does not queue, and the set still builds a server.
using ServerSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Modules(AtDataMode)
		.Command<"+CIPSEND">().Write(Argument<uint16>("length")).Prompt().Finals("SEND OK").Failures("SEND FAIL")
		.Urc<"+IPD">(Argument<uint16>("length"), Data())
		.Urc<"READY">();
}>;


struct ServerApp {
};


void TestServerAnswersPromptWithError() {
	ServerApp app;
	Wire wire;
	AtServer<ServerSet, ServerApp, AtServerBinding<>> server(app, wire);

	for (char character : std::string_view("AT+CIPSEND=5\r\n")) {
		server.Feed(static_cast<uint8>(character));
	}

	server.Process();
	AT_TEXT(wire.bytes, "\r\nERROR\r\n");
}


void TestServerUrcAfterPromptError() {
	ServerApp app;
	Wire wire;
	AtServer<ServerSet, ServerApp, AtServerBinding<>> server(app, wire);

	AT_TRUE(server.Urc<"READY">() == AtStatus{});

	for (char character : std::string_view("AT+CIPSEND=5\r\n")) {
		server.Feed(static_cast<uint8>(character));
	}

	server.Process();
	AT_TEXT(wire.bytes, "\r\nERROR\r\n\r\nREADY\r\n");
}


}


int main() {
	TestSendWritesDataAfterPrompt();
	TestPromptNeedsLineStart();
	TestPromptIgnoredOutsidePromptRequest();
	TestSecondDataRequestWaitsForFirst();
	TestSendDataBusyWhenFull();
	TestSendZeroLength();
	TestDataChunksOfRun();
	TestDataLengthZero();
	TestDataLengthOverflowRefused();
	TestDataDuringReadKeepsRequest();
	TestServerAnswersPromptWithError();
	TestServerUrcAfterPromptError();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtData: %d of %d checks failed\n", failureCount, checkCount);
		return 1;
	}

	std::printf("TestAtData: all %d checks passed\n", checkCount);
	return 0;
}
