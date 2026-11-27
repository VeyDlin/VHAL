#include <VHAL.h>
#include <Utilities/AT/AtClient.h>
#include <Utilities/AT/AtServer.h>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

// Host tests of the declared ranges of integer arguments. The server refuses a value outside its range in the way it refuses a value that
// overflows its type, so the reply is ERROR. The client drops a URC field and a Read reply in the same way as an overflow, and it does not
// check the values it sends. The test text prints the declared range, not the range of the type.


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
			"TestAtRange.cpp:%d: %s\n  actual:   \"%s\"\n  expected: \"%s\"\n",
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
		std::fprintf(stderr, "TestAtRange.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
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


// The output writer: the text the server or the client writes, in the order it is written.
struct CapturePrint : public Print {
	std::string text;

	void WriteRaw(const char* data, size_t size) override {
		text.append(data, size);
	}
};


// The server's application: what its handlers received.
struct App {
	int calls = 0;
	int a = -1;
	int b = -1;
	uint32 u = 0;
	bool hasD = false;
	int d = -1;
	int x = -1;
	bool hasY = false;
	int y = -1;
	int basic = -1;
};


AtStatus Rng(AtCall<App> call, uint8 a, int8 b, uint32 u, std::optional<uint8> d) {
	call.app.calls++;
	call.app.a = a;
	call.app.b = b;
	call.app.u = u;
	call.app.hasD = d.has_value();
	call.app.d = d ? static_cast<int>(*d) : -1;
	return AtStatus{};
}


AtStatus Opt(AtCall<App> call, uint8 x, std::optional<uint16> y) {
	call.app.x = x;
	call.app.hasY = y.has_value();
	call.app.y = y ? static_cast<int>(*y) : -1;
	return AtStatus{};
}


AtStatus BasicX(AtCall<App> call, uint8 on) {
	call.app.basic = on;
	return AtStatus{};
}


// The description: a test form whose text prints the declared ranges, a write form with an optional argument that has a range, a second
// write form with an optional unsigned argument, and a basic command with a ranged value.
using Ranged = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(2000))
		.Command<"+RNG">()
			.Test()
			.Write(
				Argument<uint8>("a").Range(10, 20),
				Argument<int8>("b").Range(-5, 5),
				Argument<uint32>("u").Range(100LL, 4000000000LL),
				Optional<uint8>("d").Range(3, 7))
		.Command<"+OPT">()
			.Write(Argument<uint8>("x").Range(1, 9), Optional<uint16>("y").Range(0, 500))
		.Basic<"X">().Value(Argument<uint8>("on").Range(1, 3));
}>;


using RangedBinding = AtServerBinding<
	AtOn<"+RNG", &Rng>,
	AtOn<"+OPT", &Opt>,
	AtOn<"X", &BasicX>
>;


// The declared ranges, as the set keeps them for the runtime checks, in the field numbering: +RNG has fields 0 to 3, and +OPT has 4 and 5.
static_assert(Ranged::fieldRanges[0].minimum == 10);
static_assert(Ranged::fieldRanges[0].maximum == 20);
static_assert(Ranged::fieldRanges[1].minimum == -5);
static_assert(Ranged::fieldRanges[2].maximum == 4000000000);
static_assert(Ranged::fieldRanges[3].maximum == 7);
static_assert(Ranged::fieldRanges[4].minimum == 1);
static_assert(Ranged::fieldRanges[4].maximum == 9);
static_assert(Ranged::fieldRanges[5].minimum == 0);
static_assert(Ranged::fieldRanges[5].maximum == 500);


// A session with the server, which feeds its input and returns the text the server wrote for it.
struct Session {
	App app;
	CapturePrint out;
	AtServer<Ranged, App, RangedBinding> server{app, out};


	std::string Send(std::string_view input) {
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
};


// A value on each edge of its range is accepted, and a value one past each edge is refused. The unsigned argument is checked on both edges
// too, so its range does not follow the type's own minimum.
void TestServerRanges() {
	Session s;

	AT_TEXT(s.Send("AT+RNG=10,-5,100\r"), Ok());
	AT_EQ(s.app.a, 10);
	AT_EQ(s.app.b, -5);
	AT_EQ(s.app.u, 100);
	AT_EQ(s.app.hasD, 0);
	AT_TEXT(s.Send("AT+RNG=20,5,4000000000\r"), Ok());
	AT_EQ(s.app.a, 20);
	AT_EQ(s.app.b, 5);
	AT_EQ(s.app.u, 4000000000LL);

	AT_TEXT(s.Send("AT+RNG=9,0,100\r"), Error());
	AT_TEXT(s.Send("AT+RNG=21,0,100\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,-6,100\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,6,100\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,0,99\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,0,4000000001\r"), Error());

	// A value past the type is refused the same way, and the handler is not called for any refused line.
	AT_TEXT(s.Send("AT+RNG=256,0,100\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,0,100,-1\r"), Error());
	AT_EQ(s.app.calls, 2);
}


// An optional argument with a range: an empty field is absent and needs no check, an edge value is accepted, and one past an edge is refused.
void TestOptionalRanges() {
	Session s;

	AT_TEXT(s.Send("AT+RNG=10,0,100,3\r"), Ok());
	AT_EQ(s.app.hasD, 1);
	AT_EQ(s.app.d, 3);
	AT_TEXT(s.Send("AT+RNG=10,0,100,7\r"), Ok());
	AT_EQ(s.app.d, 7);
	AT_TEXT(s.Send("AT+RNG=10,0,100,2\r"), Error());
	AT_TEXT(s.Send("AT+RNG=10,0,100,8\r"), Error());

	AT_TEXT(s.Send("AT+RNG=10,0,100,\r"), Ok());
	AT_EQ(s.app.hasD, 0);
	AT_TEXT(s.Send("AT+RNG=10,0,100\r"), Ok());
	AT_EQ(s.app.hasD, 0);

	AT_TEXT(s.Send("AT+OPT=1,500\r"), Ok());
	AT_EQ(s.app.hasY, 1);
	AT_EQ(s.app.y, 500);
	AT_TEXT(s.Send("AT+OPT=1,501\r"), Error());
	AT_TEXT(s.Send("AT+OPT=1,0\r"), Ok());
	AT_EQ(s.app.y, 0);
	AT_TEXT(s.Send("AT+OPT=9,\r"), Ok());
	AT_EQ(s.app.hasY, 0);
	AT_TEXT(s.Send("AT+OPT=0,1\r"), Error());
	AT_TEXT(s.Send("AT+OPT=10,1\r"), Error());
}


// The test text prints the declared range of each argument. An optional argument's range is printed too, and a bool or a string keeps its own.
void TestTestText() {
	Session s;

	AT_TEXT(s.Send("AT+RNG=?\r"), Info("+RNG: (10-20),(-5-5),(100-4000000000),(3-7)") + Ok());
}


// A basic command's value is refused outside its range, the same way.
void TestBasicRange() {
	Session s;

	AT_TEXT(s.Send("ATX1\r"), Ok());
	AT_EQ(s.app.basic, 1);
	AT_TEXT(s.Send("ATX3\r"), Ok());
	AT_EQ(s.app.basic, 3);
	AT_TEXT(s.Send("ATX0\r"), Error());
	AT_TEXT(s.Send("ATX4\r"), Error());
	AT_EQ(s.app.basic, 3);
}


// The client: a URC field is dropped when it is outside its range, the same as when it overflows its type. A Read reply that overflows its
// type is invalidData, and the client does not check the values it sends.
using ClientSet = AtCommandSet<+[] {
	return AtCommandSetBuilder()
		.Defaults(AtDefaults().TimeoutMs(100))
		.Command<"+V">().Read<uint8>("v")
		.Command<"+LED">().Write(Argument<uint8>("level").Range(10, 20))
		.Urc<"+IPD">(Argument<uint8>("n").Range(10, 20), Optional<uint8>("o").Range(3, 7));
}>;


std::string handled;


void OnRead(Result<uint8, AtStatus> result) {
	if (result.IsOk()) {
		handled += "read " + std::to_string(static_cast<int>(result.Value())) + "\n";
	} else {
		handled += result.Error().status == ResultStatus::invalidData ? "read invalidData\n" : "read other\n";
	}
}


void OnStatus(AtStatus status) {
	handled += status.status == ResultStatus::ok ? "status ok\n" : "status other\n";
}


void OnIpd(uint8 n, std::optional<uint8> o) {
	handled += "ipd " + std::to_string(static_cast<int>(n)) + " ";
	handled += o ? std::to_string(static_cast<int>(*o)) : std::string("-");
	handled += "\n";
}


using ClientBinding = AtClientBinding<AtUrcOn<"+IPD", &OnIpd>>;
using Client = AtClient<ClientSet, ClientBinding>;


// Feeds the text to the client, and runs the client at time zero.
void Feed(Client& client, std::string_view bytes) {
	for (char character : bytes) {
		client.Feed(static_cast<uint8>(character));
	}

	client.Process(0);
}


void TestClientUrcRanges() {
	handled.clear();
	CapturePrint out;
	Client client(out);

	Feed(client, "+IPD,10\r\n");
	Feed(client, "+IPD,20\r\n");
	Feed(client, "+IPD,9\r\n");
	Feed(client, "+IPD,21\r\n");
	Feed(client, "+IPD,300\r\n");
	AT_TEXT(handled, "ipd 10 -\nipd 20 -\n");

	handled.clear();
	Feed(client, "+IPD,15,3\r\n");
	Feed(client, "+IPD,15,7\r\n");
	Feed(client, "+IPD,15,2\r\n");
	Feed(client, "+IPD,15,8\r\n");
	Feed(client, "+IPD,15,\r\n");
	AT_TEXT(handled, "ipd 15 3\nipd 15 7\nipd 15 -\n");
}


void TestClientReadRange() {
	handled.clear();
	CapturePrint out;
	Client client(out);

	client.Read<"+V", &OnRead>();
	client.Process(0);
	Feed(client, "+V: 300\r\nOK\r\n");
	AT_TEXT(handled, "read invalidData\n");

	handled.clear();
	client.Read<"+V", &OnRead>();
	client.Process(0);
	Feed(client, "+V: 255\r\nOK\r\n");
	AT_TEXT(handled, "read 255\n");
}


// A value outside the declared range is sent as it is: the client encodes what the caller gives, and the module decides.
void TestClientSendsUnchecked() {
	handled.clear();
	CapturePrint out;
	Client client(out);

	AtStatus queued = client.Write<"+LED", &OnStatus>(static_cast<uint8>(5));
	AT_EQ(queued == AtStatus{}, 1);
	client.Process(0);
	AT_TEXT(out.text, "AT+LED=5\r\n");
	Feed(client, "\r\nOK\r\n");
	AT_TEXT(handled, "status ok\n");
}


} // namespace


int main() {
	TestServerRanges();
	TestOptionalRanges();
	TestTestText();
	TestBasicRange();
	TestClientUrcRanges();
	TestClientReadRange();
	TestClientSendsUnchecked();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtRange: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestAtRange: all %d checks passed\n", checkCount);
	return 0;
}
