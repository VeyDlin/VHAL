// Byte-exact WebSocket client tests against a scripted socket: handshake, framing, masking, fragmentation, control
// frames, the close handshake, every protocol error from the brief, send-side fragmentation and the allocator path.
#include "ScriptedSocket.h"
#include <TestHeap.h>
#include <VhalCheck.h>
#include <Drivers/Interface/Network/WebSocket/WebSocketClient.h>
#include <Utilities/Encoding/Base/Base64.h>
#include <Utilities/Memory/Heap.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>


static const char kDefaultHead[] =
	"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {ACCEPT}\r\n\r\n";


struct Harness {
	ScriptedSocket socket;
	TestRandom random;
	TestHeap<Memory::IncrementalDefrag<8>> memory;
	WebSocketClient client;
	std::vector<std::string> texts;
	std::vector<std::vector<uint8>> binaries;
	std::vector<std::pair<uint16, std::string>> closes;


	explicit Harness(size_t heapBytes = 262144) : memory(heapBytes), client(socket, memory) {
		socket.answerHandshake = true;
		socket.handshakeHead = kDefaultHead;
		client.onText = [this](std::string_view text) {
			texts.emplace_back(text);
		};
		client.onBinary = [this](std::span<const uint8> data) {
			binaries.emplace_back(data.begin(), data.end());
		};
		client.onClose = [this](uint16 code, std::string_view reason) {
			closes.emplace_back(code, std::string(reason));
		};
	}


	WebSocketClient::ConnectOptions Options() {
		WebSocketClient::ConnectOptions options;
		options.randomSource = [this]() {
			return random.Next();
		};
		options.clockMs = [this]() {
			return socket.nowMs;
		};
		return options;
	}


	ResultStatus Connect(const char* url = "ws://example.test:8080/v1/events?x=1") {
		return client.Connect(url, Options());
	}


	// Bytes the client wrote after the handshake request.
	std::vector<uint8> Sent() const {
		return std::vector<uint8>(socket.written.begin() + static_cast<std::ptrdiff_t>(socket.requestEnd), socket.written.end());
	}


	std::string Request() const {
		return std::string(socket.written.begin(), socket.written.begin() + static_cast<std::ptrdiff_t>(socket.requestEnd));
	}
};


static std::vector<uint8> CodeBytes(uint16 code) {
	return { static_cast<uint8>(code >> 8), static_cast<uint8>(code) };
}


static std::string ExpectedKey() {
	std::array<uint8, 16> nonce{};
	for (uint32 call = 0; call < 4; call++) {
		uint32 value = 0x9E3779B9u * (call + 1);
		nonce[call * 4] = static_cast<uint8>(value >> 24);
		nonce[call * 4 + 1] = static_cast<uint8>(value >> 16);
		nonce[call * 4 + 2] = static_cast<uint8>(value >> 8);
		nonce[call * 4 + 3] = static_cast<uint8>(value);
	}

	char key[32] = {};
	size_t length = Base64::Encode(std::span<char>(key, sizeof(key)), std::span<const uint8>(nonce));
	return std::string(key, length);
}


// Connects with the default answer and checks that it succeeded. Returns false (after recording) otherwise.
static bool ConnectOk(Harness& h) {
	ResultStatus status = h.Connect();
	VHAL_CHECK_EQ(status, ResultStatus::ok);
	return status == ResultStatus::ok;
}


// Protocol error: the server sends serverBytes, the client must close with expectedCode and report expectedStatus.
static void ExpectFailure(const std::vector<uint8>& serverBytes, uint16 expectedCode, ResultStatus expectedStatus) {
	Harness h;
	if (!ConnectOk(h)) {
		return;
	}

	h.socket.Queue(serverBytes);
	ResultStatus status = h.client.Poll(100);
	VHAL_CHECK_EQ(status, expectedStatus);
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, expectedCode);
	}

	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.client.GetCloseCode(), expectedCode);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x8, TestRandom::MaskFor(0), CodeBytes(expectedCode)));
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


// ---------------------------------------------------------------------------------------------------------------------
// Handshake

static void HandshakeRequestAndAccept() {
	Harness h;
	VHAL_CHECK_EQ(h.Connect("ws://example.test/v1/stream?model=x"), ResultStatus::ok);
	std::string request = h.Request();
	VHAL_CHECK(request.rfind("GET /v1/stream?model=x HTTP/1.1\r\n", 0) == 0);
	VHAL_CHECK(request.find("\r\nHost: example.test\r\n") != std::string::npos);
	VHAL_CHECK(request.find("\r\nUpgrade: websocket\r\n") != std::string::npos);
	VHAL_CHECK(request.find("\r\nConnection: Upgrade\r\n") != std::string::npos);
	VHAL_CHECK(request.find("\r\nSec-WebSocket-Version: 13\r\n") != std::string::npos);
	VHAL_CHECK(request.find("\r\nSec-WebSocket-Key: " + ExpectedKey() + "\r\n") != std::string::npos);
	VHAL_CHECK(request.size() >= 4 && request.substr(request.size() - 4) == "\r\n\r\n");
	VHAL_CHECK_EQ(h.client.GetHandshakeStatus(), 101);
	VHAL_CHECK(h.client.IsOpen());
	VHAL_CHECK_EQ(h.socket.lastHost, "example.test");
	VHAL_CHECK_EQ(h.socket.lastPort, 80);
	VHAL_CHECK(h.socket.GetParameters().protocol == SocketAdapter::Protocol::Tcp);
}


static void SecureUrlUsesTlsAndPort443() {
	Harness h;
	VHAL_CHECK_EQ(h.Connect("wss://api.example.test/v1/stream?model=test"), ResultStatus::ok);
	VHAL_CHECK_EQ(h.socket.lastPort, 443);
	VHAL_CHECK(h.socket.GetParameters().protocol == SocketAdapter::Protocol::Tls);
	VHAL_CHECK(h.Request().find("\r\nHost: api.example.test\r\n") != std::string::npos);
}


static void NonDefaultPortIsInHostHeader() {
	Harness h;
	VHAL_CHECK_EQ(h.Connect("wss://h.test:8443/"), ResultStatus::ok);
	VHAL_CHECK_EQ(h.socket.lastPort, 8443);
	VHAL_CHECK(h.Request().find("\r\nHost: h.test:8443\r\n") != std::string::npos);
	VHAL_CHECK(h.Request().rfind("GET / HTTP/1.1\r\n", 0) == 0);
}


static void HeadersAndSubprotocolAreSent() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nupgrade: WebSocket\r\nconnection: keep-alive, Upgrade\r\n"
		"Sec-WebSocket-Protocol: chat\r\nSec-WebSocket-Accept: {ACCEPT}\r\n\r\n";
	static const std::string_view subprotocols[] = { "superchat", "chat" };
	static const WebSocketClient::Header headers[] = { { "Authorization", "Bearer abc" } };
	WebSocketClient::ConnectOptions options = h.Options();
	options.headers = headers;
	options.subprotocols = subprotocols;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	VHAL_CHECK(h.Request().find("\r\nAuthorization: Bearer abc\r\n") != std::string::npos);
	VHAL_CHECK(h.Request().find("\r\nSec-WebSocket-Protocol: superchat, chat\r\n") != std::string::npos);
	VHAL_CHECK_EQ(h.client.GetSubprotocol(), "chat");
}


static void BadAcceptIsProtocolError() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Accept: AAAAAAAAAAAAAAAAAAAAAAAAAAA=\r\n\r\n";
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::protocolError);
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


static void MissingUpgradeIsProtocolError() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: h2c\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {ACCEPT}\r\n\r\n";
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::protocolError);
}


static void ConnectionWithoutUpgradeTokenIsProtocolError() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: keep-alive\r\n"
		"Sec-WebSocket-Accept: {ACCEPT}\r\n\r\n";
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::protocolError);
}


static void UnrequestedSubprotocolIsProtocolError() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Protocol: other\r\nSec-WebSocket-Accept: {ACCEPT}\r\n\r\n";
	static const std::string_view subprotocols[] = { "chat" };
	WebSocketClient::ConnectOptions options = h.Options();
	options.subprotocols = subprotocols;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::protocolError);
}


static void UnrequestedExtensionIsProtocolError() {
	Harness h;
	h.socket.handshakeHead =
		"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Extensions: permessage-deflate\r\nSec-WebSocket-Accept: {ACCEPT}\r\n\r\n";
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::protocolError);
}


static void UnauthorizedKeepsStatusAndBody() {
	Harness h;
	std::string body = R"({"error":"invalid api key"})";
	h.socket.handshakeHead = "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: " +
		std::to_string(body.size()) + "\r\n\r\n";
	h.socket.handshakeTail = Bytes(body);
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::authenticationFailed);
	VHAL_CHECK_EQ(h.client.GetHandshakeStatus(), 401);
	VHAL_CHECK_EQ(h.client.GetRejectBody(), body);
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


static void ForbiddenChunkedBodyIsDechunked() {
	Harness h;
	std::string body = "forbidden: model not allowed";
	char size[32];
	std::snprintf(size, sizeof(size), "%zx", body.size());
	h.socket.handshakeHead = "HTTP/1.1 403 Forbidden\r\nTransfer-Encoding: chunked\r\n\r\n";
	h.socket.handshakeTail = Bytes(std::string(size) + "\r\n" + body + "\r\n0\r\n\r\n");
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::authorizationFailed);
	VHAL_CHECK_EQ(h.client.GetHandshakeStatus(), 403);
	VHAL_CHECK_EQ(h.client.GetRejectBody(), body);
}


static void RejectBodyIsLimitedByOption() {
	Harness h;
	std::string body = "abcdefghijklmnop";
	h.socket.handshakeHead = "HTTP/1.1 400 Bad Request\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n";
	h.socket.handshakeTail = Bytes(body);
	WebSocketClient::ConnectOptions options = h.Options();
	options.maxRejectBodyBytes = 5;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::connectionRefused);
	VHAL_CHECK_EQ(h.client.GetRejectBody(), "abcde");
	VHAL_CHECK_EQ(h.client.GetHandshakeStatus(), 400);
}


// Any well-formed non-101 answer other than 401 and 403 is a refusal: the status and body stay readable.
static void RefusedStatusesKeepStatusAndBody() {
	static const uint16 codes[] = { 400, 404, 429, 500 };
	for (uint16 code : codes) {
		Harness h;
		std::string body = "{\"code\":\"refused-" + std::to_string(code) + "\",\"error\":\"nope\"}";
		h.socket.handshakeHead = "HTTP/1.1 " + std::to_string(code) + " Reason\r\nContent-Type: application/json\r\n"
			"Content-Length: " + std::to_string(body.size()) + "\r\n\r\n";
		h.socket.handshakeTail = Bytes(body);
		VHAL_CHECK_EQ(h.Connect(), ResultStatus::connectionRefused);
		VHAL_CHECK_EQ(h.client.GetHandshakeStatus(), code);
		VHAL_CHECK_EQ(h.client.GetRejectBody(), body);
		VHAL_CHECK(!h.client.IsOpen());
		VHAL_CHECK_EQ(h.socket.closeCount, 1);
	}
}


// The connect timeout goes to Open(); reads use the I/O timeout, which the handshake sets to its remaining time.
static void ConnectTimeoutIsSeparateFromIoTimeout() {
	Harness h;
	WebSocketClient::ConnectOptions options = h.Options();
	options.connectTimeoutMs = 3000;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	VHAL_CHECK_EQ(h.socket.connectTimeoutAtOpen, 3000);
	VHAL_CHECK(!h.socket.readTimeouts.empty());
	if (!h.socket.readTimeouts.empty()) {
		VHAL_CHECK_EQ(h.socket.readTimeouts[0], 10000);
	}
}


// Poll(0) must not wait: with nothing buffered, the read runs with a zero I/O timeout and returns timeout at once.
static void PollZeroDoesNotWait() {
	Harness h;
	ConnectOk(h);
	uint32 before = h.socket.nowMs;
	VHAL_CHECK_EQ(h.client.Poll(0), ResultStatus::timeout);
	VHAL_CHECK_EQ(h.socket.ioTimeoutMs, 0);
	VHAL_CHECK_EQ(h.socket.nowMs, before);
	VHAL_CHECK(h.client.IsOpen());
}


// A frame arriving one byte per poll is reassembled once its last byte has been read.
static void FrameSplitAcrossShortPollsIsReassembled() {
	Harness h;
	ConnectOk(h);
	std::vector<uint8> frame = ServerFrame(true, 0x1, Bytes("hello"));
	for (uint8 byte : frame) {
		h.socket.QueueText(std::string(1, static_cast<char>(byte)));
		h.socket.QueueStatus(ResultStatus::timeout);
	}

	int polls = 0;
	while (h.texts.empty() && polls < 100) {
		h.client.Poll(0);
		polls++;
	}

	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "hello");
	}

	VHAL_CHECK(h.client.IsOpen());
}


static void SilentServerTimesOut() {
	Harness h;
	h.socket.answerHandshake = false;
	WebSocketClient::ConnectOptions options = h.Options();
	options.handshakeTimeoutMs = 3000;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::timeout);
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


static void HandshakeLeftoverFrameIsProcessed() {
	Harness h;
	h.socket.handshakeTail = ServerFrame(true, 0x1, Bytes("hi"));
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::ok);
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "hi");
	}
}


static void InvalidUrlsAndOptionsAreRejected() {
	Harness h;
	VHAL_CHECK_EQ(h.Connect("http://example.test/"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://h:0/"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://h:70000/"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://h:80x/"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://h/#fragment"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://user@h/"), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.Connect("ws://[::1]:8080/"), ResultStatus::notSupported);
	VHAL_CHECK_EQ(h.Connect(nullptr), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.socket.openCount, 0);

	WebSocketClient::ConnectOptions noRandom = h.Options();
	noRandom.randomSource = nullptr;
	VHAL_CHECK_EQ(h.client.Connect("ws://h/", noRandom), ResultStatus::invalidParameter);

	static const WebSocketClient::Header reserved[] = { { "Sec-WebSocket-Key", "x" } };
	WebSocketClient::ConnectOptions withReserved = h.Options();
	withReserved.headers = reserved;
	VHAL_CHECK_EQ(h.client.Connect("ws://h/", withReserved), ResultStatus::invalidParameter);

	static const WebSocketClient::Header badValue[] = { { "X-Name", "a\r\nInjected: 1" } };
	WebSocketClient::ConnectOptions withBadValue = h.Options();
	withBadValue.headers = badValue;
	VHAL_CHECK_EQ(h.client.Connect("ws://h/", withBadValue), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.socket.openCount, 0);
}


static void SecondConnectWhileOpenIsBusy() {
	Harness h;
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::ok);
	VHAL_CHECK_EQ(h.Connect(), ResultStatus::busy);
}


// ---------------------------------------------------------------------------------------------------------------------
// Receiving: lengths, fragments, control frames

static void ReceiveTextAndBinaryAtLengthBoundaries() {
	const size_t lengths[] = { 0, 1, 125, 126, 127, 65535, 65536 };
	for (size_t length : lengths) {
		Harness h;
		if (!ConnectOk(h)) {
			return;
		}

		std::vector<uint8> payload(length, 0x41);
		h.socket.Queue(ServerFrame(true, 0x2, payload));
		VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
		VHAL_CHECK_EQ(h.binaries.size(), 1);
		if (!h.binaries.empty()) {
			VHAL_CHECK(h.binaries[0] == payload);
		}
	}
}


static void ReceiveSingleFrameTextMessage() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(true, 0x1, Bytes("{\"type\":\"event.partial\"}")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "{\"type\":\"event.partial\"}");
	}
}


static void ReceiveFragmentedMessage() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(false, 0x1, Bytes("Hel")));
	h.socket.Queue(ServerFrame(true, 0x0, Bytes("lo")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "Hello");
	}
}


// A control frame may arrive between the fragments of a data message (RFC 6455 5.4).
static void PingInsideFragmentedMessageIsAnsweredAndMessageCompletes() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(false, 0x1, Bytes("Hel")));
	h.socket.Queue(ServerFrame(true, 0x9, Bytes("p")));
	h.socket.Queue(ServerFrame(true, 0x0, Bytes("lo")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0xA, TestRandom::MaskFor(0), Bytes("p")));
	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "Hello");
	}
}


static void PingIsAnsweredWithSamePayload() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(true, 0x9, Bytes("abc")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0xA, TestRandom::MaskFor(0), Bytes("abc")));
}


static void UnsolicitedPongIsIgnored() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(true, 0xA, Bytes("x")));
	h.socket.Queue(ServerFrame(true, 0x1, Bytes("after")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK(h.Sent().empty());
	VHAL_CHECK_EQ(h.texts.size(), 1);
}


static void ServerCloseIsEchoedAndReported() {
	Harness h;
	ConnectOk(h);
	std::vector<uint8> payload = CodeBytes(1000);
	payload.push_back('b');
	payload.push_back('y');
	payload.push_back('e');
	h.socket.Queue(ServerFrame(true, 0x8, payload));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::connectionClosed);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x8, TestRandom::MaskFor(0), CodeBytes(1000)));
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1000);
		VHAL_CHECK_EQ(h.closes[0].second, "bye");
	}
	VHAL_CHECK_EQ(h.client.GetCloseCode(), 1000);
	VHAL_CHECK_EQ(h.client.GetCloseReason(), "bye");
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


static void ServerCloseWithoutCodeIsEchoedEmpty() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(true, 0x8, {}));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::connectionClosed);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x8, TestRandom::MaskFor(0), {}));
	VHAL_CHECK_EQ(h.client.GetCloseCode(), 1005);
}


static void ClientCloseWaitsForPeerClose() {
	Harness h;
	ConnectOk(h);
	std::vector<uint8> reply = CodeBytes(1000);
	h.socket.Queue(ServerFrame(true, 0x8, reply));
	VHAL_CHECK_EQ(h.client.Close(1000, "done"), ResultStatus::ok);
	std::vector<uint8> expected = CodeBytes(1000);
	expected.push_back('d');
	expected.push_back('o');
	expected.push_back('n');
	expected.push_back('e');
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x8, TestRandom::MaskFor(0), expected));
	VHAL_CHECK_EQ(h.closes.size(), 1);
	VHAL_CHECK_EQ(h.client.GetCloseCode(), 1000);
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.client.Close(), ResultStatus::ok);
}


static void ClientCloseTimesOutWithoutPeerClose() {
	Harness h;
	WebSocketClient::ConnectOptions options = h.Options();
	options.closeTimeoutMs = 3000;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	VHAL_CHECK_EQ(h.client.Close(1000, ""), ResultStatus::timeout);
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1006);
	}
	VHAL_CHECK_EQ(h.socket.closeCount, 1);
}


static void CloseRejectsUnsendableCodesAndLongReasons() {
	Harness h;
	ConnectOk(h);
	VHAL_CHECK_EQ(h.client.Close(1005, ""), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.client.Close(1006, ""), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.client.Close(999, ""), ResultStatus::invalidParameter);
	VHAL_CHECK_EQ(h.client.Close(1000, std::string(124, 'r')), ResultStatus::invalidParameter);
	VHAL_CHECK(h.Sent().empty());
	VHAL_CHECK(h.client.IsOpen());
}


static void PeerDropWithoutCloseReportsAbnormalClosure() {
	Harness h;
	ConnectOk(h);
	h.socket.QueueStatus(ResultStatus::connectionClosed);
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::connectionClosed);
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1006);
	}
	VHAL_CHECK(!h.client.IsOpen());
}


static void ReadErrorReportsAbnormalClosure() {
	Harness h;
	ConnectOk(h);
	h.socket.QueueStatus(ResultStatus::readError);
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::readError);
	VHAL_CHECK_EQ(h.client.GetCloseCode(), 1006);
}


static void PollWithNothingReturnsTimeoutAndStaysOpen() {
	Harness h;
	ConnectOk(h);
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::timeout);
	VHAL_CHECK(h.client.IsOpen());
	VHAL_CHECK_EQ(h.closes.size(), 0);
}


static void PollOnClosedClientIsNoConnection() {
	Harness h;
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::noConnection);
	VHAL_CHECK_EQ(h.client.SendText("x"), ResultStatus::noConnection);
	VHAL_CHECK_EQ(h.client.SendBinary(std::span<const uint8>()), ResultStatus::noConnection);
	VHAL_CHECK_EQ(h.client.SendPing(std::span<const uint8>()), ResultStatus::noConnection);
	VHAL_CHECK_EQ(h.client.Close(), ResultStatus::ok);
}


// ---------------------------------------------------------------------------------------------------------------------
// Protocol errors (RFC 6455 section 7.1.7): the close code is sent, the connection fails, onClose reports it.

static void ReservedBitIsProtocolError() {
	ExpectFailure({ 0xC1, 0x01, 'a' }, 1002, ResultStatus::protocolError);
}


static void UnknownOpcodeIsProtocolError() {
	ExpectFailure({ 0x83, 0x00 }, 1002, ResultStatus::protocolError);
}


static void ReservedOpcodeIsProtocolError() {
	ExpectFailure({ 0x8B, 0x00 }, 1002, ResultStatus::protocolError);
}


static void MaskedServerFrameIsProtocolError() {
	ExpectFailure({ 0x81, 0x81, 0, 0, 0, 0, 'a' }, 1002, ResultStatus::protocolError);
}


static void OversizedControlFrameIsProtocolError() {
	ExpectFailure(ServerFrame(true, 0x9, std::vector<uint8>(126, 0)), 1002, ResultStatus::protocolError);
}


static void FragmentedControlFrameIsProtocolError() {
	ExpectFailure({ 0x09, 0x00 }, 1002, ResultStatus::protocolError);
}


static void OneByteCloseBodyIsProtocolError() {
	ExpectFailure(ServerFrame(true, 0x8, { 0x03 }), 1002, ResultStatus::protocolError);
}


static void ReservedCloseCodeIsProtocolError() {
	ExpectFailure(ServerFrame(true, 0x8, { 0x03, 0xED }), 1002, ResultStatus::protocolError);
}


static void CloseWithInvalidReasonUtf8IsInvalidData() {
	ExpectFailure(ServerFrame(true, 0x8, { 0x03, 0xE8, 0xFF }), 1007, ResultStatus::invalidData);
}


static void InvalidUtf8TextIsInvalidData() {
	ExpectFailure(ServerFrame(true, 0x1, { 0xFF }), 1007, ResultStatus::invalidData);
}


static void InvalidUtf8SplitAcrossFragmentsIsInvalidData() {
	std::vector<uint8> bytes = ServerFrame(false, 0x1, { 0xE2, 0x82 });
	bytes = Concat(bytes, ServerFrame(true, 0x0, { 'A' }));
	ExpectFailure(bytes, 1007, ResultStatus::invalidData);
}


static void ValidUtf8SplitAcrossFragmentsIsAccepted() {
	Harness h;
	ConnectOk(h);
	h.socket.Queue(ServerFrame(false, 0x1, { 0xE2, 0x82 }));
	h.socket.Queue(ServerFrame(true, 0x0, { 0xAC }));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
	VHAL_CHECK_EQ(h.texts.size(), 1);
	if (!h.texts.empty()) {
		VHAL_CHECK_EQ(h.texts[0], "\xE2\x82\xAC");
	}
}


static void ContinuationWithoutStartIsProtocolError() {
	ExpectFailure(ServerFrame(true, 0x0, Bytes("x")), 1002, ResultStatus::protocolError);
}


static void NewDataFrameDuringFragmentationIsProtocolError() {
	std::vector<uint8> bytes = ServerFrame(false, 0x1, Bytes("a"));
	bytes = Concat(bytes, ServerFrame(true, 0x1, Bytes("b")));
	ExpectFailure(bytes, 1002, ResultStatus::protocolError);
}


static void NonMinimalSixteenBitLengthIsProtocolError() {
	std::vector<uint8> bytes = { 0x82, 126, 0x00, 0x05 };
	bytes.insert(bytes.end(), 5, 'a');
	ExpectFailure(bytes, 1002, ResultStatus::protocolError);
}


static void SixtyFourBitMsbIsProtocolError() {
	ExpectFailure({ 0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 0 }, 1002, ResultStatus::protocolError);
}


static void NonMinimalSixtyFourBitLengthIsProtocolError() {
	std::vector<uint8> bytes = { 0x82, 127, 0, 0, 0, 0, 0, 0, 0x00, 0x05 };
	bytes.insert(bytes.end(), 5, 'a');
	ExpectFailure(bytes, 1002, ResultStatus::protocolError);
}


static void OversizedSingleMessageIsTooLong() {
	Harness h;
	WebSocketClient::ConnectOptions options = h.Options();
	options.maxMessageSize = 10;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	h.socket.Queue(ServerFrame(true, 0x2, std::vector<uint8>(11, 0x41)));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::messageTooLong);
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1009);
	}
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x8, TestRandom::MaskFor(0), CodeBytes(1009)));
}


static void FragmentsOverLimitAreTooLong() {
	Harness h;
	WebSocketClient::ConnectOptions options = h.Options();
	options.maxMessageSize = 10;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	h.socket.Queue(ServerFrame(false, 0x2, Bytes("12345")));
	h.socket.Queue(ServerFrame(true, 0x0, Bytes("678901")));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::messageTooLong);
	VHAL_CHECK_EQ(h.client.GetCloseCode(), 1009);
}


// ---------------------------------------------------------------------------------------------------------------------
// Sending

static void SendTextIsMaskedWithFreshKey() {
	Harness h;
	ConnectOk(h);
	VHAL_CHECK_EQ(h.client.SendText("hi"), ResultStatus::ok);
	VHAL_CHECK_EQ(h.client.SendText("hi"), ResultStatus::ok);
	std::vector<uint8> expected = Concat(
		ClientFrame(true, 0x1, TestRandom::MaskFor(0), Bytes("hi")),
		ClientFrame(true, 0x1, TestRandom::MaskFor(1), Bytes("hi")));
	VHAL_CHECK(h.Sent() == expected);
}


static void SendBinaryLengthBoundaries() {
	const size_t lengths[] = { 0, 125, 126, 65535, 65536 };
	for (size_t length : lengths) {
		Harness h;
		ConnectOk(h);
		std::vector<uint8> payload(length);
		for (size_t i = 0; i < length; i++) {
			payload[i] = static_cast<uint8>(i * 7 + 3);
		}

		VHAL_CHECK_EQ(h.client.SendBinary(payload), ResultStatus::ok);
		VHAL_CHECK(h.Sent() == ClientFrame(true, 0x2, TestRandom::MaskFor(0), payload));
	}
}


static void SendFragmentsLargeMessages() {
	Harness h;
	WebSocketClient::ConnectOptions options = h.Options();
	options.fragmentSize = 4;
	VHAL_CHECK_EQ(h.client.Connect("ws://example.test/", options), ResultStatus::ok);
	VHAL_CHECK_EQ(h.client.SendText("abcdefghij"), ResultStatus::ok);
	std::vector<uint8> expected = Concat(
		Concat(ClientFrame(false, 0x1, TestRandom::MaskFor(0), Bytes("abcd")),
			ClientFrame(false, 0x0, TestRandom::MaskFor(1), Bytes("efgh"))),
		ClientFrame(true, 0x0, TestRandom::MaskFor(2), Bytes("ij")));
	VHAL_CHECK(h.Sent() == expected);
}


static void SendRejectsInvalidInput() {
	Harness h;
	ConnectOk(h);
	VHAL_CHECK_EQ(h.client.SendText("\xFF"), ResultStatus::invalidParameter);
	std::vector<uint8> tooLong(126, 0);
	VHAL_CHECK_EQ(h.client.SendPing(tooLong), ResultStatus::invalidParameter);
	VHAL_CHECK(h.Sent().empty());
}


static void SendPingCarriesPayload() {
	Harness h;
	ConnectOk(h);
	std::vector<uint8> payload = { 'x' };
	VHAL_CHECK_EQ(h.client.SendPing(payload), ResultStatus::ok);
	VHAL_CHECK(h.Sent() == ClientFrame(true, 0x9, TestRandom::MaskFor(0), Bytes("x")));
}


static void WriteFailureDropsConnection() {
	Harness h;
	ConnectOk(h);
	h.socket.failWriteCall = h.socket.writeCalls + 1;
	VHAL_CHECK_EQ(h.client.SendText("x"), ResultStatus::writeError);
	VHAL_CHECK(!h.client.IsOpen());
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1006);
	}
}


// ---------------------------------------------------------------------------------------------------------------------
// Allocator path (Memory::Heap with defragmentation)

static void MessageBufferSurvivesDefragmentationBetweenFragments() {
	Harness h;
	ConnectOk(h);

	// Live blocks around a hole make the defragmenter move the message buffer when it runs.
	Memory::Handle first = h.memory.Allocate(300);
	Memory::Handle second = h.memory.Allocate(300);
	h.memory.Deallocate(first);
	VHAL_CHECK(h.memory.FragmentationPercent() > 0);

	std::string part1(1000, 'a');
	std::string part2(1000, 'b');
	std::string part3(500, 'c');
	h.socket.Queue(ServerFrame(false, 0x2, Bytes(part1)));
	VHAL_CHECK_EQ(h.client.Poll(0), ResultStatus::ok);
	h.memory.Defragment();
	h.socket.Queue(ServerFrame(false, 0x0, Bytes(part2)));
	VHAL_CHECK_EQ(h.client.Poll(0), ResultStatus::ok);
	h.memory.Defragment();
	h.socket.Queue(ServerFrame(true, 0x0, Bytes(part3)));
	VHAL_CHECK_EQ(h.client.Poll(0), ResultStatus::ok);

	std::string expected = part1 + part2 + part3;
	VHAL_CHECK_EQ(h.binaries.size(), 1);
	if (!h.binaries.empty()) {
		VHAL_CHECK(std::string(h.binaries[0].begin(), h.binaries[0].end()) == expected);
	}

	h.memory.Deallocate(second);
}


static void MessagesAcrossDefragmentationAreDeliveredIntact() {
	Harness h;
	ConnectOk(h);
	Memory::Handle keep = h.memory.Allocate(64);
	for (int round = 0; round < 20; round++) {
		Memory::Handle hole = h.memory.Allocate(200 + round * 17);
		h.socket.Queue(ServerFrame(true, 0x1, Bytes("message " + std::to_string(round))));
		h.socket.Queue(ServerFrame(true, 0x2, std::vector<uint8>(100 + round, static_cast<uint8>(round))));
		// Poll(100) drains everything queued: with a fixed clock, Poll keeps reading until the socket runs dry.
		VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::ok);
		h.memory.Deallocate(hole);
		h.memory.Defragment();
	}

	VHAL_CHECK_EQ(h.texts.size(), 20);
	VHAL_CHECK_EQ(h.binaries.size(), 20);
	if (h.texts.size() == 20) {
		VHAL_CHECK_EQ(h.texts[19], "message 19");
	}
	h.memory.Deallocate(keep);
}


static void OutOfMemoryClosesWith1011() {
	Harness h(256);
	ConnectOk(h);
	h.socket.Queue(ServerFrame(true, 0x2, std::vector<uint8>(1000, 0x41)));
	VHAL_CHECK_EQ(h.client.Poll(100), ResultStatus::outOfMemory);
	VHAL_CHECK_EQ(h.closes.size(), 1);
	if (!h.closes.empty()) {
		VHAL_CHECK_EQ(h.closes[0].first, 1011);
	}
}


struct TestCase {
	const char* name;
	void (*run)();
};


int main() {
	const TestCase cases[] = {
		{ "ws-handshake-request-and-accept", HandshakeRequestAndAccept },
		{ "ws-secure-url-uses-tls-port-443", SecureUrlUsesTlsAndPort443 },
		{ "ws-non-default-port-in-host", NonDefaultPortIsInHostHeader },
		{ "ws-headers-and-subprotocol", HeadersAndSubprotocolAreSent },
		{ "ws-bad-accept-rejected", BadAcceptIsProtocolError },
		{ "ws-missing-upgrade-rejected", MissingUpgradeIsProtocolError },
		{ "ws-connection-without-upgrade-token", ConnectionWithoutUpgradeTokenIsProtocolError },
		{ "ws-unrequested-subprotocol-rejected", UnrequestedSubprotocolIsProtocolError },
		{ "ws-unrequested-extension-rejected", UnrequestedExtensionIsProtocolError },
		{ "ws-401-keeps-status-and-body", UnauthorizedKeepsStatusAndBody },
		{ "ws-403-chunked-body-dechunked", ForbiddenChunkedBodyIsDechunked },
		{ "ws-reject-body-limit", RejectBodyIsLimitedByOption },
		{ "ws-refused-400-404-429-500-keep-body", RefusedStatusesKeepStatusAndBody },
		{ "ws-connect-timeout-separate-from-io", ConnectTimeoutIsSeparateFromIoTimeout },
		{ "ws-poll-zero-does-not-wait", PollZeroDoesNotWait },
		{ "ws-poll-frame-split-across-polls", FrameSplitAcrossShortPollsIsReassembled },
		{ "ws-handshake-timeout", SilentServerTimesOut },
		{ "ws-handshake-leftover-frame", HandshakeLeftoverFrameIsProcessed },
		{ "ws-invalid-urls-and-options", InvalidUrlsAndOptionsAreRejected },
		{ "ws-connect-while-open-busy", SecondConnectWhileOpenIsBusy },
		{ "ws-receive-length-boundaries", ReceiveTextAndBinaryAtLengthBoundaries },
		{ "ws-receive-single-text", ReceiveSingleFrameTextMessage },
		{ "ws-receive-fragmented", ReceiveFragmentedMessage },
		{ "ws-ping-inside-fragments", PingInsideFragmentedMessageIsAnsweredAndMessageCompletes },
		{ "ws-ping-answered-same-payload", PingIsAnsweredWithSamePayload },
		{ "ws-unsolicited-pong-ignored", UnsolicitedPongIsIgnored },
		{ "ws-server-close-echoed", ServerCloseIsEchoedAndReported },
		{ "ws-server-close-without-code", ServerCloseWithoutCodeIsEchoedEmpty },
		{ "ws-client-close-waits-for-peer", ClientCloseWaitsForPeerClose },
		{ "ws-client-close-timeout", ClientCloseTimesOutWithoutPeerClose },
		{ "ws-close-rejects-bad-input", CloseRejectsUnsendableCodesAndLongReasons },
		{ "ws-peer-drop-is-1006", PeerDropWithoutCloseReportsAbnormalClosure },
		{ "ws-read-error-is-1006", ReadErrorReportsAbnormalClosure },
		{ "ws-poll-timeout-stays-open", PollWithNothingReturnsTimeoutAndStaysOpen },
		{ "ws-closed-client-no-connection", PollOnClosedClientIsNoConnection },
		{ "ws-err-reserved-bit-1002", ReservedBitIsProtocolError },
		{ "ws-err-unknown-opcode-1002", UnknownOpcodeIsProtocolError },
		{ "ws-err-reserved-opcode-1002", ReservedOpcodeIsProtocolError },
		{ "ws-err-masked-server-frame-1002", MaskedServerFrameIsProtocolError },
		{ "ws-err-oversized-control-1002", OversizedControlFrameIsProtocolError },
		{ "ws-err-fragmented-control-1002", FragmentedControlFrameIsProtocolError },
		{ "ws-err-one-byte-close-1002", OneByteCloseBodyIsProtocolError },
		{ "ws-err-reserved-close-code-1002", ReservedCloseCodeIsProtocolError },
		{ "ws-err-close-reason-utf8-1007", CloseWithInvalidReasonUtf8IsInvalidData },
		{ "ws-err-text-utf8-1007", InvalidUtf8TextIsInvalidData },
		{ "ws-err-utf8-split-1007", InvalidUtf8SplitAcrossFragmentsIsInvalidData },
		{ "ws-utf8-split-valid", ValidUtf8SplitAcrossFragmentsIsAccepted },
		{ "ws-err-continuation-without-start-1002", ContinuationWithoutStartIsProtocolError },
		{ "ws-err-new-data-while-fragmented-1002", NewDataFrameDuringFragmentationIsProtocolError },
		{ "ws-err-non-minimal-16bit-1002", NonMinimalSixteenBitLengthIsProtocolError },
		{ "ws-err-64bit-msb-1002", SixtyFourBitMsbIsProtocolError },
		{ "ws-err-non-minimal-64bit-1002", NonMinimalSixtyFourBitLengthIsProtocolError },
		{ "ws-err-single-message-too-long-1009", OversizedSingleMessageIsTooLong },
		{ "ws-err-fragments-too-long-1009", FragmentsOverLimitAreTooLong },
		{ "ws-send-text-masked-fresh-key", SendTextIsMaskedWithFreshKey },
		{ "ws-send-binary-length-boundaries", SendBinaryLengthBoundaries },
		{ "ws-send-fragmented", SendFragmentsLargeMessages },
		{ "ws-send-rejects-bad-input", SendRejectsInvalidInput },
		{ "ws-send-ping-payload", SendPingCarriesPayload },
		{ "ws-write-failure-drops", WriteFailureDropsConnection },
		{ "ws-heap-defrag-mid-message", MessageBufferSurvivesDefragmentationBetweenFragments },
		{ "ws-heap-defrag-between-messages", MessagesAcrossDefragmentationAreDeliveredIntact },
		{ "ws-heap-out-of-memory-1011", OutOfMemoryClosesWith1011 }
	};

	for (const TestCase& testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	return VhalCheck::Finish("TestFrame");
}
