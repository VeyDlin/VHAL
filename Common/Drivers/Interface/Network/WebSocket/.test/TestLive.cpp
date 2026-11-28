// Integration tests of the WebSocket client against a real server (ws_server.py, the websockets library), over POSIX sockets.
// The test starts the server itself and ends it when it finishes. It prints VHAL_SKIP: and exits 0 when Python 3, the websockets
// package or the POSIX socket headers are missing.
#include <LoopbackServer.h>
#include <Utilities/Memory/IAllocator.h>
#include <VhalCheck.h>
#include <Drivers/Interface/Network/WebSocket/WebSocketClient.h>
#include <Utilities/Memory/Heap.h>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <thread>
#include <vector>


#if __has_include(<sys/socket.h>) && __has_include(<netdb.h>)
#define VHAL_TEST_POSIX_SOCKETS 1
#include "PosixSocketAdapter.h"
#endif


#if defined(VHAL_TEST_POSIX_SOCKETS)

static uint16 gPort = 0;


struct LiveClient {
	PosixSocketAdapter socket;
	Memory::Heap<Memory::IncrementalDefrag<8>> heap;
	Memory::AllocatorOf<Memory::Heap<Memory::IncrementalDefrag<8>>> memory;
	WebSocketClient client;
	std::vector<std::string> texts;
	std::vector<std::vector<uint8>> binaries;
	std::vector<std::pair<uint16, std::string>> closes;
	std::random_device entropy;


	LiveClient() : heap(262144), memory(heap), client(socket, memory) {
		socket.SetTimeout(200);
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
			return entropy();
		};
		options.clockMs = []() {
			std::chrono::steady_clock::duration now = std::chrono::steady_clock::now().time_since_epoch();
			return static_cast<uint32>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
		};
		return options;
	}


	ResultStatus Connect(const std::string& path, const WebSocketClient::ConnectOptions& options) {
		std::string url = "ws://127.0.0.1:" + std::to_string(gPort) + path;
		return client.Connect(url.c_str(), options);
	}


	ResultStatus Connect(const std::string& path) {
		return Connect(path, Options());
	}


	// Polls until done() is true, a non-timeout status comes back, or about 5 seconds have passed.
	template <typename Predicate>
	ResultStatus PollUntil(Predicate done) {
		ResultStatus status = ResultStatus::timeout;
		for (int attempt = 0; attempt < 25 && !done(); attempt++) {
			status = client.Poll(200);
			if (status != ResultStatus::ok && status != ResultStatus::timeout) {
				break;
			}
		}

		return status;
	}
};


static std::vector<uint8> Pattern(size_t length, uint8 seed) {
	std::vector<uint8> data(length);
	for (size_t i = 0; i < length; i++) {
		data[i] = static_cast<uint8>(seed + i * 13);
	}

	return data;
}


static void LiveEchoTextAndBinary() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/echo"), ResultStatus::ok);
	VHAL_CHECK(c.client.IsOpen());
	VHAL_CHECK_EQ(c.client.GetHandshakeStatus(), 101);

	VHAL_CHECK_EQ(c.client.SendText("hello, server"), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.texts.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "hello, server");
	}

	std::vector<uint8> payload = Pattern(3200, 7);
	VHAL_CHECK_EQ(c.client.SendBinary(payload), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.binaries.empty(); });
	VHAL_CHECK_EQ(c.binaries.size(), 1);
	if (!c.binaries.empty()) {
		VHAL_CHECK(c.binaries[0] == payload);
	}

	VHAL_CHECK_EQ(c.client.Close(1000, "done"), ResultStatus::ok);
	VHAL_CHECK(!c.client.IsOpen());
	VHAL_CHECK_EQ(c.closes.size(), 1);
	if (!c.closes.empty()) {
		VHAL_CHECK_EQ(c.closes[0].first, 1000);
	}
}


// Poll(0) on a quiet connection returns within a few milliseconds, not after the socket's read timeout, and the echo
// is still picked up by later Poll(0) calls.
static void LivePollZeroReturnsAtOnce() {
	LiveClient c;
	c.socket.SetIoTimeout(500);
	VHAL_CHECK_EQ(c.Connect("/echo"), ResultStatus::ok);

	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
	VHAL_CHECK_EQ(c.client.Poll(0), ResultStatus::timeout);
	long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();
	VHAL_CHECK(elapsedMs < 50);

	VHAL_CHECK_EQ(c.client.SendText("zero"), ResultStatus::ok);
	for (int attempt = 0; attempt < 5000 && c.texts.empty() && c.client.IsOpen(); attempt++) {
		c.client.Poll(0);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "zero");
	}

	c.client.Close();
}


static void LiveServerPingIsAnsweredWithPong() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/ping"), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.texts.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "pong-received");
	}
	c.client.Close();
}


static void LiveServerCloseCodeIsReported() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/close/4001"), ResultStatus::ok);
	VHAL_CHECK_EQ(c.client.SendText("ping me"), ResultStatus::ok);
	VHAL_CHECK_EQ(c.PollUntil([&c]() { return !c.client.IsOpen(); }), ResultStatus::connectionClosed);
	VHAL_CHECK_EQ(c.client.GetCloseCode(), 4001);
	VHAL_CHECK_EQ(c.client.GetCloseReason(), "bye");
	VHAL_CHECK_EQ(c.closes.size(), 1);
}


static void LiveFragmentedMessagesAreReassembled() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/fragmented"), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.texts.empty() && !c.binaries.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "Hello");
	}

	std::vector<uint8> expected = { 0x00, 0x01, 0x02 };
	VHAL_CHECK_EQ(c.binaries.size(), 1);
	if (!c.binaries.empty()) {
		VHAL_CHECK(c.binaries[0] == expected);
	}
	c.client.Close();
}


static void LiveUnauthorizedKeepsStatusAndBody() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/reject"), ResultStatus::authenticationFailed);
	VHAL_CHECK_EQ(c.client.GetHandshakeStatus(), 401);
	VHAL_CHECK_EQ(c.client.GetRejectBody(), R"({"error":"invalid api key"})");
	VHAL_CHECK(!c.client.IsOpen());
}


static void LiveForbiddenKeepsStatusAndBody() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/forbidden"), ResultStatus::authorizationFailed);
	VHAL_CHECK_EQ(c.client.GetHandshakeStatus(), 403);
	VHAL_CHECK_EQ(c.client.GetRejectBody(), "forbidden: model not allowed");
}


static void LiveAuthorizationHeaderIsSent() {
	LiveClient c;
	static const WebSocketClient::Header headers[] = { { "Authorization", "Bearer test-token" } };
	WebSocketClient::ConnectOptions options = c.Options();
	options.headers = headers;
	VHAL_CHECK_EQ(c.Connect("/auth", options), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.texts.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "auth:Bearer test-token");
	}
	c.client.Close();

	LiveClient anonymous;
	VHAL_CHECK_EQ(anonymous.Connect("/auth"), ResultStatus::authenticationFailed);
}


static void LiveSubprotocolIsNegotiated() {
	LiveClient c;
	static const std::string_view subprotocols[] = { "superchat", "chat" };
	WebSocketClient::ConnectOptions options = c.Options();
	options.subprotocols = subprotocols;
	VHAL_CHECK_EQ(c.Connect("/subprotocol", options), ResultStatus::ok);
	VHAL_CHECK_EQ(c.client.GetSubprotocol(), "chat");
	c.PollUntil([&c]() { return !c.texts.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], "chat");
	}
	c.client.Close();
}


static void LiveMessageAtLimitIsDelivered() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/big/65536"), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.binaries.empty(); });
	VHAL_CHECK_EQ(c.binaries.size(), 1);
	if (!c.binaries.empty()) {
		VHAL_CHECK_EQ(c.binaries[0].size(), 65536);
	}
	c.client.Close();
}


static void LiveMessageOverLimitClosesWith1009() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/big/65537"), ResultStatus::ok);
	VHAL_CHECK_EQ(c.PollUntil([&c]() { return !c.client.IsOpen(); }), ResultStatus::messageTooLong);
	VHAL_CHECK_EQ(c.client.GetCloseCode(), 1009);
	VHAL_CHECK_EQ(c.binaries.size(), 0);
}


// Fragments the client sends must be reassembled by a real parser (the server echoes one message).
static void LiveClientFragmentsAreAcceptedByServer() {
	LiveClient c;
	WebSocketClient::ConnectOptions options = c.Options();
	options.fragmentSize = 7;
	VHAL_CHECK_EQ(c.Connect("/echo", options), ResultStatus::ok);
	std::string text = "the quick brown fox jumps over the lazy dog";
	VHAL_CHECK_EQ(c.client.SendText(text), ResultStatus::ok);
	std::vector<uint8> payload = Pattern(1000, 3);
	VHAL_CHECK_EQ(c.client.SendBinary(payload), ResultStatus::ok);
	c.PollUntil([&c]() { return !c.texts.empty() && !c.binaries.empty(); });
	VHAL_CHECK_EQ(c.texts.size(), 1);
	if (!c.texts.empty()) {
		VHAL_CHECK_EQ(c.texts[0], text);
	}

	VHAL_CHECK_EQ(c.binaries.size(), 1);
	if (!c.binaries.empty()) {
		VHAL_CHECK(c.binaries[0] == payload);
	}
	c.client.Close();
}


// The shape of a streaming event session: events arrive as text frames, then the server closes normally.
static void LiveEventStreamEndsWithNormalClose() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/events"), ResultStatus::ok);
	ResultStatus status = c.PollUntil([&c]() { return !c.client.IsOpen(); });
	VHAL_CHECK_EQ(status, ResultStatus::connectionClosed);
	VHAL_CHECK_EQ(c.texts.size(), 3);
	if (c.texts.size() == 3) {
		VHAL_CHECK(c.texts[2].find("\"text\":\"hello world\"") != std::string::npos);
	}
	VHAL_CHECK_EQ(c.client.GetCloseCode(), 1000);
}


// Paced 100 ms audio frames, polled between sends, as the prototype does. The echo server returns each frame.
static void LivePacedAudioFramesWithPolling() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/echo"), ResultStatus::ok);
	const size_t frames = 10;
	for (size_t i = 0; i < frames; i++) {
		VHAL_CHECK_EQ(c.client.SendBinary(Pattern(3200, static_cast<uint8>(i))), ResultStatus::ok);
		ResultStatus polled = c.client.Poll(0);
		VHAL_CHECK(polled == ResultStatus::ok || polled == ResultStatus::timeout);
	}
	c.PollUntil([&c]() { return c.binaries.size() >= frames; });
	VHAL_CHECK_EQ(c.binaries.size(), frames);
	c.client.Close();
}


// Defragmentation between messages over real sockets: the Heap may move every live block between Polls.
static void LiveHeapDefragmentsBetweenMessages() {
	LiveClient c;
	VHAL_CHECK_EQ(c.Connect("/echo"), ResultStatus::ok);
	Memory::Handle keep = c.heap.Allocate(128);
	Memory::Handle hole = c.heap.Allocate(512);
	c.heap.Deallocate(hole);

	size_t sent = 0;
	for (size_t round = 0; round < 12; round++) {
		size_t length = 500 + round * 900;
		std::vector<uint8> payload = Pattern(length, static_cast<uint8>(round));
		VHAL_CHECK_EQ(c.client.SendBinary(payload), ResultStatus::ok);
		c.PollUntil([&c, round]() { return c.binaries.size() > round; });
		c.heap.Defragment();
		sent++;
	}

	VHAL_CHECK_EQ(c.binaries.size(), sent);
	if (c.binaries.size() == sent) {
		VHAL_CHECK(c.binaries[11] == Pattern(500 + 11 * 900, 11));
	}
	c.heap.Deallocate(keep);
	c.client.Close();
}


static void LiveWssNeedsTlsAdapter() {
	LiveClient c;
	std::string url = "wss://127.0.0.1:" + std::to_string(gPort) + "/echo";
	VHAL_CHECK_EQ(c.client.Connect(url.c_str(), c.Options()), ResultStatus::notSupported);
	VHAL_CHECK(!c.client.IsOpen());
}


static void LiveRefusedPortIsNoConnection() {
	LiveClient c;
	std::string url = "ws://127.0.0.1:1/echo";
	VHAL_CHECK_EQ(c.client.Connect(url.c_str(), c.Options()), ResultStatus::noConnection);
}


struct TestCase {
	const char* name;
	void (*run)();
};


int main() {
	std::string python = VhalLoopback::FindPython();
	if (python.empty()) {
		std::printf("VHAL_SKIP: Python 3 is not installed\n");
		return 0;
	}

	if (!VhalLoopback::RunSilently(python + " -c \"import websockets.sync.server\"")) {
		std::printf("VHAL_SKIP: the websockets package is not installed for %s\n", python.c_str());
		return 0;
	}

	VhalLoopback::Server server;
	std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "ws_server.py";
	gPort = server.Start(python, script.string(), "", "ws_server");
	VHAL_CHECK(gPort != 0);
	if (gPort == 0) {
		std::fprintf(stderr, "the WebSocket server did not report a port, see ws_server.log\n");
		return VhalCheck::Finish("TestLive");
	}

	const TestCase cases[] = {
		{ "live-echo-text-binary-close", LiveEchoTextAndBinary },
		{ "live-poll-zero-returns-at-once", LivePollZeroReturnsAtOnce },
		{ "live-server-ping-pong", LiveServerPingIsAnsweredWithPong },
		{ "live-server-close-code", LiveServerCloseCodeIsReported },
		{ "live-fragmented-reassembly", LiveFragmentedMessagesAreReassembled },
		{ "live-401-status-and-body", LiveUnauthorizedKeepsStatusAndBody },
		{ "live-403-status-and-body", LiveForbiddenKeepsStatusAndBody },
		{ "live-authorization-header", LiveAuthorizationHeaderIsSent },
		{ "live-subprotocol", LiveSubprotocolIsNegotiated },
		{ "live-message-at-64k-limit", LiveMessageAtLimitIsDelivered },
		{ "live-message-over-limit-1009", LiveMessageOverLimitClosesWith1009 },
		{ "live-client-fragments-accepted", LiveClientFragmentsAreAcceptedByServer },
		{ "live-event-stream-close", LiveEventStreamEndsWithNormalClose },
		{ "live-paced-audio-polling", LivePacedAudioFramesWithPolling },
		{ "live-heap-defrag-between-messages", LiveHeapDefragmentsBetweenMessages },
		{ "live-wss-needs-tls-adapter", LiveWssNeedsTlsAdapter },
		{ "live-refused-port", LiveRefusedPortIsNoConnection }
	};

	for (const TestCase& testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	server.Stop();
	return VhalCheck::Finish("TestLive");
}


#else


int main() {
	std::printf("VHAL_SKIP: the POSIX socket headers are not available on this platform\n");
	return 0;
}


#endif
