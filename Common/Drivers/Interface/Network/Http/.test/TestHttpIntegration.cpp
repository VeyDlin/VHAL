// Integration tests: HttpClient and ServerSentEvents against http_server.py on loopback TCP. The test starts the server itself
// and ends it when it finishes. It prints VHAL_SKIP: and exits 0 when Python 3 or the POSIX socket headers are missing.
#include <LoopbackServer.h>
#include <TestHeap.h>
#include <Utilities/Callback/Delegate.h>
#include <VhalCheck.h>
#include <Drivers/Interface/Network/Http/HttpClient.h>
#include <Drivers/Interface/Network/Http/ServerSentEvents.h>
#include <Utilities/Memory/Heap.h>
#include <Utilities/Serialization/JSON/JSON.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <span>
#include <string>
#include <vector>


#if __has_include(<sys/socket.h>) && __has_include(<netdb.h>)
#define VHAL_TEST_POSIX_SOCKETS 1
#include "PosixSocketAdapter.h"
#endif


#if defined(VHAL_TEST_POSIX_SOCKETS)

using Method = HttpClient::Method;


namespace {

int testPort = 0;


// The request holds a view of its URL, so the string must outlive the request. Callers keep it in a named variable.
std::string Url(const char *path) {
	return "http://127.0.0.1:" + std::to_string(testPort) + path;
}


std::string Collect(HttpClient& client, ResultStatus& status) {
	std::string body;
	status = client.ReadBody([&body](std::span<const uint8> piece) -> ResultStatus {
		body.append(reinterpret_cast<const char *>(piece.data()), piece.size());
		return ResultStatus::ok;
	});
	return body;
}


// Sends one GET on a fresh client and reads the whole body.
std::string Get(const char *path, ResultStatus& sendStatus, ResultStatus& bodyStatus, uint16& code) {
	PosixSocketAdapter socket;
	TestHeap<> heap(65536);
	HttpClient client(socket, heap);
	std::string url = Url(path);
	HttpClient::Request request;
	request.url = url;
	sendStatus = client.Send(request);
	if (sendStatus != ResultStatus::ok) {
		return std::string();
	}

	code = client.GetStatusCode();
	return Collect(client, bodyStatus);
}


// Decodes choices[0].delta.content from one chat chunk. Returns false when the chunk has no content.
bool DeltaContent(std::string_view chunk, std::string& content) {
	std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(chunk, "choices"), 0);
	std::string_view delta = JSON::GetRaw(choice, "delta");
	char text[256];
	size_t length = 0;
	if (!JSON::GetString(delta, "content", std::span<char>(text, sizeof(text)), length)) {
		return false;
	}

	content.assign(text, length);
	return true;
}


struct StreamResult {
	std::vector<std::string> data;
	std::string content;
};


// Receives the events of a parser into a StreamResult. The parser reaches OnEvent through a delegate bound to this object.
class StreamSink {
public:
	explicit StreamSink(StreamResult& target)
		: result(target) {
	}


	void OnEvent(std::string_view type, std::string_view data, std::string_view id) {
		(void)type;
		(void)id;
		result.data.push_back(std::string(data));
		std::string piece;
		if (DeltaContent(data, piece)) {
			result.content += piece;
		}
	}

private:
	StreamResult& result;
};


// Streams a text/event-stream body into a parser. The parser's buffers come from memory, the same heap as the client.
ResultStatus StreamEvents(HttpClient& client, Memory::IAllocator& memory, StreamResult& result) {
	StreamSink sink(result);
	ServerSentEvents parser(memory, 65536);
	parser.onEvent = Callback::Delegate<void(std::string_view, std::string_view, std::string_view)>::Bind<StreamSink, &StreamSink::OnEvent>(&sink);

	return client.ReadBody([&parser](std::span<const uint8> piece) -> ResultStatus {
		parser.Feed(piece);
		return ResultStatus::ok;
	});
}


void CheckStream(const StreamResult& result, ResultStatus bodyStatus) {
	VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
	VHAL_CHECK_EQ(result.data.size(), 5);
	if (!result.data.empty()) {
		VHAL_CHECK_EQ(result.data.back(), "[END]");
	}

	VHAL_CHECK_EQ(result.content, "Привет, мир");
}


// POSTs the chat request to path and streams the answer. With defragment, the heap is defragmented before every socket
// read, so the head and the event buffers move while they are being assembled.
void PostStream(const char *path, bool defragment) {
	PosixSocketAdapter socket;
	TestHeap<Memory::IncrementalDefrag<8>> heap(65536);
	Memory::Handle gap = heap.Allocate(8);
	Memory::Handle live = heap.Allocate(64);
	heap.Deallocate(gap);
	void *before = live.Data();
	if (defragment) {
		socket.beforeRead = [&heap]() -> void {
			heap.Defragment();
		};
	}

	HttpClient client(socket, heap);
	std::string url = Url(path);
	const char *payload = "{\"model\":\"test-model\",\"stream\":true}";
	HttpClient::Header headers[] = {
		{ "Authorization", "Bearer test-key" },
		{ "Content-Type", "application/json" },
	};
	HttpClient::Request request;
	request.method = Method::Post;
	request.url = url;
	request.headers = headers;
	request.body = std::span<const uint8>(reinterpret_cast<const uint8 *>(payload), std::char_traits<char>::length(payload));

	ResultStatus sendStatus = client.Send(request);
	VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
	if (sendStatus != ResultStatus::ok) {
		return;
	}

	VHAL_CHECK_EQ(client.GetStatusCode(), 200);
	if (client.GetStatusCode() != 200) {
		return;
	}

	StreamResult result;
	ResultStatus bodyStatus = StreamEvents(client, heap, result);
	CheckStream(result, bodyStatus);

	if (defragment) {
		VHAL_CHECK(live.Data() != before);
	}
}

}


struct TestCase {
	const char *name;
	void (*run)();
};


int main() {
	std::string python = VhalLoopback::FindPython();
	if (python.empty()) {
		std::printf("VHAL_SKIP: Python 3 is not installed\n");
		return 0;
	}

	VhalLoopback::Server server;
	std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "http_server.py";
	testPort = server.Start(python, script.string(), "", "http_server");
	VHAL_CHECK(testPort != 0);
	if (testPort == 0) {
		std::fprintf(stderr, "the HTTP server did not report a port, see http_server.log\n");
		return VhalCheck::Finish("TestHttpIntegration");
	}

	const TestCase cases[] = {
		{ "integ-content-length", []() -> void {
			ResultStatus sendStatus = ResultStatus::ok;
			ResultStatus bodyStatus = ResultStatus::ok;
			uint16 code = 0;
			std::string body = Get("/length", sendStatus, bodyStatus, code);
			VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(code, 200);
			VHAL_CHECK_EQ(body, "hello world");
		} },

		{ "integ-chunked-extensions-and-trailers", []() -> void {
			ResultStatus sendStatus = ResultStatus::ok;
			ResultStatus bodyStatus = ResultStatus::ok;
			uint16 code = 0;
			std::string body = Get("/chunked", sendStatus, bodyStatus, code);
			VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(body, "The quick brown fox jumps over the lazy dog.");
		} },

		{ "integ-close-delimited-body", []() -> void {
			ResultStatus sendStatus = ResultStatus::ok;
			ResultStatus bodyStatus = ResultStatus::ok;
			uint16 code = 0;
			std::string body = Get("/close", sendStatus, bodyStatus, code);
			VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(body, "close-delimited body");
		} },

		{ "integ-http10-response", []() -> void {
			ResultStatus sendStatus = ResultStatus::ok;
			ResultStatus bodyStatus = ResultStatus::ok;
			uint16 code = 0;
			std::string body = Get("/http10", sendStatus, bodyStatus, code);
			VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(code, 200);
			VHAL_CHECK_EQ(body, "http10 body");
		} },

		{ "integ-keepalive-reuses-one-socket", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			ResultStatus bodyStatus = ResultStatus::ok;
			std::string urlCount = Url("/count");
			std::string urlLength = Url("/length");
			std::string urlChunked = Url("/chunked");

			HttpClient::Request request;
			request.url = urlCount;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string firstCount = Collect(client, bodyStatus);
			const std::string *paths[] = { &urlLength, &urlChunked, &urlLength };
			for (const std::string *path : paths) {
				request.url = *path;
				VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

				Collect(client, bodyStatus);
				VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			}

			request.url = urlCount;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string secondCount = Collect(client, bodyStatus);
			VHAL_CHECK_EQ(firstCount, secondCount);
		} },

		{ "integ-connection-close-reconnects", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			ResultStatus bodyStatus = ResultStatus::ok;
			std::string urlCount = Url("/count");
			std::string urlClose = Url("/close");

			HttpClient::Request request;
			request.url = urlCount;
			client.Send(request);
			int before = std::stoi(Collect(client, bodyStatus));

			request.url = urlClose;
			client.Send(request);
			Collect(client, bodyStatus);

			request.url = urlCount;
			client.Send(request);
			int after = std::stoi(Collect(client, bodyStatus));
			VHAL_CHECK_EQ(after, before + 1);
		} },

		{ "integ-interim-responses-skipped", []() -> void {
			ResultStatus sendStatus = ResultStatus::ok;
			ResultStatus bodyStatus = ResultStatus::ok;
			uint16 code = 0;
			std::string body = Get("/interim", sendStatus, bodyStatus, code);
			VHAL_CHECK_EQ(sendStatus, ResultStatus::ok);
			VHAL_CHECK_EQ(code, 200);
			VHAL_CHECK_EQ(body, "ok");
		} },

		{ "integ-redirect-is-returned-not-followed", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/redirect");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(client.GetStatusCode(), 301);

			std::string_view location;
			VHAL_CHECK(client.FindHeader("location", location));
			VHAL_CHECK_EQ(location, "/length");
		} },

		{ "integ-head-has-no-body", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/length");
			HttpClient::Request request;
			request.method = Method::Head;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			ResultStatus bodyStatus = ResultStatus::ok;
			std::string body = Collect(client, bodyStatus);
			VHAL_CHECK_EQ(bodyStatus, ResultStatus::ok);
			VHAL_CHECK(body.empty());

			request.method = Method::Get;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			body = Collect(client, bodyStatus);
			VHAL_CHECK_EQ(body, "hello world");
		} },

		{ "integ-post-echoes-body-and-headers", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/echo");
			const char *payload = "{\"prompt\":\"hi\"}";
			HttpClient::Header headers[] = {
				{ "Authorization", "Bearer test-key" },
				{ "Content-Type", "application/json" },
			};
			HttpClient::Request request;
			request.method = Method::Post;
			request.url = url;
			request.headers = headers;
			request.body = std::span<const uint8>(reinterpret_cast<const uint8 *>(payload), std::char_traits<char>::length(payload));
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string_view seen;
			std::string_view contentType;
			client.FindHeader("X-Authorization-Seen", seen);
			client.FindHeader("X-Content-Type-Seen", contentType);
			VHAL_CHECK_EQ(seen, "Bearer test-key");
			VHAL_CHECK_EQ(contentType, "application/json");

			ResultStatus bodyStatus = ResultStatus::ok;
			std::string echoed = Collect(client, bodyStatus);
			VHAL_CHECK_EQ(echoed, payload);
		} },

		{ "integ-readbodyto-fits", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/length");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			uint8 buffer[64] = {};
			uint32 size = 0;
			ResultStatus status = client.ReadBodyTo(std::span<uint8>(buffer, sizeof(buffer)), size);
			VHAL_CHECK_EQ(status, ResultStatus::ok);
			VHAL_CHECK_EQ(size, 11);
			VHAL_CHECK_EQ(std::string(reinterpret_cast<const char *>(buffer), size), "hello world");
		} },

		{ "integ-readbodyto-too-small", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/length");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			uint8 buffer[4] = {};
			uint32 size = 0;
			ResultStatus status = client.ReadBodyTo(std::span<uint8>(buffer, sizeof(buffer)), size);
			VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
		} },

		{ "integ-sse-chunk-stream-odd-slices", []() -> void {
			PostStream("/sse", false);
		} },

		{ "integ-sse-chunk-stream-crlf", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/sse-crlf");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			StreamResult result;
			ResultStatus bodyStatus = StreamEvents(client, heap, result);
			CheckStream(result, bodyStatus);
		} },

		{ "integ-defragment-during-head-and-events", []() -> void {
			PostStream("/sse", true);
		} },

		{ "integ-stall-times-out", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			client.options.timeoutMs = 300;
			std::string url = Url("/stall");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::timeout);
		} },

		{ "integ-https-without-tls-is-not-supported", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			HttpClient::Request request;
			request.url = "https://127.0.0.1:1/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::notSupported);
		} },

		{ "integ-unread-body-then-next-request", []() -> void {
			PosixSocketAdapter socket;
			TestHeap<> heap(65536);
			HttpClient client(socket, heap);
			std::string url = Url("/length");
			HttpClient::Request request;
			request.url = url;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			ResultStatus bodyStatus = ResultStatus::ok;
			std::string body = Collect(client, bodyStatus);
			VHAL_CHECK_EQ(body, "hello world");
		} },
	};

	for (const TestCase &testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	server.Stop();
	return VhalCheck::Finish("TestHttpIntegration");
}


#else


int main() {
	std::printf("VHAL_SKIP: the POSIX socket headers are not available on this platform\n");
	return 0;
}


#endif
