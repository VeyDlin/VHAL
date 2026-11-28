// Unit tests for HttpClient against a scripted socket. Every response is also replayed split at every byte boundary.
#include "FakeSocket.h"
#include <VHAL.h>
#include <Utilities/Memory/IAllocator.h>
#include <VhalCheck.h>
#include <Drivers/Interface/Network/Http/HttpClient.h>
#include <Utilities/Memory/Heap.h>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>


using Method = HttpClient::Method;
using Options = HttpClient::Options;


namespace {

struct Outcome {
	ResultStatus sendStatus = ResultStatus::ok;
	uint16 status = 0;
	std::string body;
	ResultStatus bodyStatus = ResultStatus::ok;
	int openCount = 0;
	std::string written;
};


Outcome Fetch(const std::vector<std::string>& slices, const char *url, Method method, const Options& options) {
	Outcome outcome;
	FakeSocket socket;
	socket.slices = slices;
	Memory::Heap<> heap(65536);
	Memory::AllocatorOf<Memory::Heap<>> memory(heap);
	HttpClient client(socket, memory);
	client.options = options;

	HttpClient::Request request;
	request.method = method;
	request.url = url;
	outcome.sendStatus = client.Send(request);
	if (outcome.sendStatus == ResultStatus::ok) {
		outcome.status = client.GetStatusCode();
		outcome.bodyStatus = client.ReadBody([&outcome](std::span<const uint8> piece) -> ResultStatus {
			outcome.body.append(reinterpret_cast<const char *>(piece.data()), piece.size());
			return ResultStatus::ok;
		});
	}

	outcome.openCount = socket.openCount;
	outcome.written = socket.written;
	return outcome;
}


Outcome Fetch(const std::vector<std::string>& slices, const char *url) {
	return Fetch(slices, url, Method::Get, Options());
}


std::vector<std::string> TwoSlices(const std::string& wire, std::size_t at) {
	return { wire.substr(0, at), wire.substr(at) };
}


std::vector<std::string> OneByteSlices(const std::string& wire) {
	std::vector<std::string> slices;
	for (char character : wire) {
		slices.push_back(std::string(1, character));
	}

	return slices;
}


// The outcome of one response: the send, the status, the body read and the body itself.
void CheckOutcome(const Outcome& outcome, uint16 expectedStatus, const std::string& expectedBody) {
	VHAL_CHECK_EQ(outcome.sendStatus, ResultStatus::ok);
	VHAL_CHECK_EQ(outcome.status, expectedStatus);
	VHAL_CHECK_EQ(outcome.bodyStatus, ResultStatus::ok);
	VHAL_CHECK_EQ(outcome.body, expectedBody);
}


// Replays wire split into two packets at every possible byte, then one byte at a time.
void CheckEverySplit(const std::string& wire, uint16 expectedStatus, const std::string& expectedBody) {
	for (std::size_t at = 1; at < wire.size(); at++) {
		CheckOutcome(Fetch(TwoSlices(wire, at), "http://example.test/"), expectedStatus, expectedBody);
	}

	CheckOutcome(Fetch(OneByteSlices(wire), "http://example.test/"), expectedStatus, expectedBody);
}


const char *ChunkedWire =
	"HTTP/1.1 200 OK\r\n"
	"Transfer-Encoding: chunked\r\n"
	"Content-Type: text/plain\r\n"
	"\r\n"
	"4;name=\"value\"\r\nWiki\r\n"
	"5\r\npedia\r\n"
	"E;x=1\r\n in\r\n\r\nchunks.\r\n"
	"0;last\r\n"
	"X-Checksum: abc\r\n"
	"\r\n";

const char *ChunkedBody = "Wikipedia in\r\n\r\nchunks.";

}


namespace {

void ChunkedBadSize(const char *size) {
	std::string wire = std::string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") + size + "\r\nabc\r\n0\r\n\r\n";
	Outcome outcome = Fetch(TwoSlices(wire, 40), "http://example.test/");
	VHAL_CHECK_EQ(outcome.bodyStatus, ResultStatus::invalidData);
}


void SendStatusOf(const char *wire, ResultStatus expected) {
	Outcome outcome = Fetch({ wire }, "http://example.test/");
	VHAL_CHECK_EQ(outcome.sendStatus, expected);
}


void BodyStatusOf(const char *wire, ResultStatus expected) {
	Outcome outcome = Fetch({ wire }, "http://example.test/");
	VHAL_CHECK_EQ(outcome.sendStatus, ResultStatus::ok);
	VHAL_CHECK_EQ(outcome.bodyStatus, expected);
}

}


static void ChunkedEverySplitCase() {
	CheckEverySplit(ChunkedWire, 200, ChunkedBody);
}


int main() {
	const struct TestCase {
		const char *name;
		void (*run)();
	} cases[] = {
		{ "http-content-length-every-split", []() -> void {
			CheckEverySplit("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello", 200, "hello");
		} },

		{ "http-chunked-every-split", ChunkedEverySplitCase },

		{ "http-chunked-zero-length-and-no-data-after-last", []() -> void {
			CheckEverySplit("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", 200, "");
		} },

		{ "http-chunked-bad-size-letters", []() -> void {
			ChunkedBadSize("zz");
		} },

		{ "http-chunked-empty-size-line", []() -> void {
			ChunkedBadSize("");
		} },

		{ "http-chunked-size-overflow", []() -> void {
			ChunkedBadSize("FFFFFFFFFFFFFFFFF");
		} },

		{ "http-chunked-missing-crlf-after-data", []() -> void {
			BodyStatusOf("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcXY0\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-chunked-trailer-over-limit", []() -> void {
			Options options;
			options.maxHeaderBytes = 64;
			std::string trailers(200, 'x');
			std::string wire = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-Big: " + trailers + "\r\n\r\n";
			Outcome outcome = Fetch({ wire }, "http://example.test/", Method::Get, options);
			VHAL_CHECK_EQ(outcome.bodyStatus, ResultStatus::outOfRange);
		} },

		{ "http-length-conflict-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\nhello", ResultStatus::invalidData);
		} },

		{ "http-length-equal-duplicates-accepted", []() -> void {
			CheckEverySplit("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nContent-Length: 5\r\nContent-Length: 5, 5\r\n\r\nhello", 200, "hello");
		} },

		{ "http-length-not-digits-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length: +5\r\n\r\nhello", ResultStatus::invalidData);
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length: 5x\r\n\r\nhello", ResultStatus::invalidData);
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-length-and-chunked-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-transfer-coding-not-final-chunked-reads-to-close", []() -> void {
			CheckEverySplit("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\nraw-bytes", 200, "raw-bytes");
		} },

		{ "http-no-length-reads-to-close", []() -> void {
			CheckEverySplit("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nabc\r\ndef", 200, "abc\r\ndef");
		} },

		{ "http-204-has-no-body", []() -> void {
			BodyStatusOf("HTTP/1.1 204 No Content\r\nContent-Length: 5\r\n\r\n", ResultStatus::ok);
		} },

		{ "http-304-has-no-body", []() -> void {
			CheckOutcome(Fetch({ "HTTP/1.1 304 Not Modified\r\nContent-Length: 5\r\n\r\n" }, "http://example.test/"), 304, "");
		} },

		{ "http-head-response-has-no-body", []() -> void {
			CheckOutcome(Fetch({ "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n" }, "http://example.test/", Method::Head, Options()), 200, "");
		} },

		{ "http-http10-reads-to-close", []() -> void {
			CheckEverySplit("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nold server", 200, "old server");
		} },

		{ "http-truncated-length-body-is-connection-closed", []() -> void {
			BodyStatusOf("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nabc", ResultStatus::connectionClosed);
		} },

		{ "http-header-block-too-large-is-out-of-range", []() -> void {
			Options options;
			options.maxHeaderBytes = 64;
			std::string wire = "HTTP/1.1 200 OK\r\nX-Long: " + std::string(100, 'v') + "\r\n\r\n";
			Outcome outcome = Fetch({ wire }, "http://example.test/", Method::Get, options);
			VHAL_CHECK_EQ(outcome.sendStatus, ResultStatus::outOfRange);
		} },

		{ "http-obs-fold-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nX-A: one\r\n two\r\nContent-Length: 0\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-header-without-colon-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nBadLine\r\nContent-Length: 0\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-whitespace-before-colon-refused", []() -> void {
			SendStatusOf("HTTP/1.1 200 OK\r\nContent-Length : 0\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-interim-responses-skipped", []() -> void {
			std::string wire =
				"HTTP/1.1 100 Continue\r\n\r\n"
				"HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n"
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";
			CheckEverySplit(wire, 200, "ok");
		} },

		{ "http-status-line-variants", []() -> void {
			// No reason phrase, leading empty lines, HTTP/2.0 refused, a status code that is not three digits.
			CheckOutcome(Fetch({ "HTTP/1.0 200\r\nContent-Length: 2\r\n\r\nok" }, "http://example.test/"), 200, "ok");
			CheckOutcome(Fetch({ "\r\n\r\nHTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n" }, "http://example.test/"), 201, "");
			SendStatusOf("HTTP/2.0 200 OK\r\nContent-Length: 0\r\n\r\n", ResultStatus::notSupported);
			SendStatusOf("HTTP/1.1 20 OK\r\nContent-Length: 0\r\n\r\n", ResultStatus::invalidData);
		} },

		{ "http-callback-abort-stops-read", []() -> void {
			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nabcdef" };
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			ResultStatus status = client.ReadBody([](std::span<const uint8>) -> ResultStatus {
				return ResultStatus::operationAborted;
			});
			VHAL_CHECK_EQ(status, ResultStatus::operationAborted);
			VHAL_CHECK(!client.IsConnected());
		} },

		{ "http-read-body-to-fits-and-overflows", []() -> void {
			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello" };
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			uint8 small[4] = {};
			uint32 size = 0;
			ResultStatus status = client.ReadBodyTo(std::span<uint8>(small, sizeof(small)), size);
			VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
		} },

		{ "http-read-body-to-copies-body", []() -> void {
			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello" };
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			uint8 buffer[16] = {};
			uint32 size = 0;
			ResultStatus status = client.ReadBodyTo(std::span<uint8>(buffer, sizeof(buffer)), size);
			VHAL_CHECK_EQ(status, ResultStatus::ok);
			VHAL_CHECK_EQ(size, 5);
			VHAL_CHECK(size == 5 && std::memcmp(buffer, "hello", 5) == 0);
		} },

		{ "http-request-bytes-are-exact", []() -> void {
			HttpClient::Header headers[] = {
				{ "Authorization", "Bearer k" },
				{ "Content-Type", "application/json" },
			};
			const uint8 payload[] = { '{', '}' };
			HttpClient::Request request;
			request.method = Method::Post;
			request.url = "http://example.test:8080/v1/resource?stream=1";
			request.headers = headers;
			request.body = payload;

			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" };
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string expected =
				"POST /v1/resource?stream=1 HTTP/1.1\r\n"
				"Host: example.test:8080\r\n"
				"User-Agent: VHAL-HttpClient/1.0\r\n"
				"Authorization: Bearer k\r\n"
				"Content-Type: application/json\r\n"
				"Content-Length: 2\r\n"
				"Connection: keep-alive\r\n"
				"\r\n"
				"{}";
			VHAL_CHECK_EQ(socket.written, expected);
		} },

		{ "http-request-injection-refused", []() -> void {
			FakeSocket socket;
			Memory::Heap<> heap(4096);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Header injected[] = { { "X-Token", "a\r\nHost: evil" } };
			HttpClient::Request request;
			request.url = "http://example.test/";
			request.headers = injected;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::invalidParameter);

			HttpClient::Header hostOverride[] = { { "Host", "other" } };
			request.headers = hostOverride;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::invalidParameter);

			HttpClient::Header badName[] = { { "Bad Name", "x" } };
			request.headers = badName;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::invalidParameter);
		} },

		{ "http-url-parsing-table", []() -> void {
			HttpClient::UrlParts parts;
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://example.test", parts), ResultStatus::ok);
			VHAL_CHECK_EQ(parts.port, 80);
			VHAL_CHECK_EQ(parts.host, "example.test");
			VHAL_CHECK(parts.target.empty());

			VHAL_CHECK_EQ(HttpClient::ParseUrl("HTTPS://Api.Example.test/v1/resource?x=1#frag", parts), ResultStatus::ok);
			VHAL_CHECK_EQ(parts.scheme, HttpClient::Scheme::Https);
			VHAL_CHECK_EQ(parts.port, 443);
			VHAL_CHECK_EQ(parts.host, "Api.Example.test");
			VHAL_CHECK_EQ(parts.target, "/v1/resource?x=1");

			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://h:8080/a", parts), ResultStatus::ok);
			VHAL_CHECK_EQ(parts.port, 8080);

			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://h?x=1", parts), ResultStatus::ok);
			VHAL_CHECK_EQ(parts.target, "?x=1");

			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://h:0/", parts), ResultStatus::invalidParameter);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://h:70000/", parts), ResultStatus::invalidParameter);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://user@h/", parts), ResultStatus::notSupported);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://[::1]/", parts), ResultStatus::notSupported);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("ftp://h/", parts), ResultStatus::notSupported);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("h/path", parts), ResultStatus::invalidParameter);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http:///x", parts), ResultStatus::invalidParameter);
			VHAL_CHECK_EQ(HttpClient::ParseUrl("http://h/a b", parts), ResultStatus::invalidParameter);
		} },

		{ "http-keepalive-reuses-connection", []() -> void {
			FakeSocket socket;
			socket.slices = {
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nab",
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\ncd",
			};
			Memory::Heap<> heap(16384);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			std::string bodies;
			for (int round = 0; round < 2; round++) {
				VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

				client.ReadBody([&bodies](std::span<const uint8> piece) -> ResultStatus {
					bodies.append(reinterpret_cast<const char *>(piece.data()), piece.size());
					return ResultStatus::ok;
				});
			}

			VHAL_CHECK_EQ(socket.openCount, 1);
			VHAL_CHECK_EQ(bodies, "abcd");
		} },

		{ "http-leftover-bytes-disable-reuse", []() -> void {
			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nabHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" };
			Memory::Heap<> heap(16384);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			client.Send(request);
			client.ReadBody([](std::span<const uint8>) -> ResultStatus {
				return ResultStatus::ok;
			});
			socket.slices.push_back("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(socket.openCount, 2);
		} },

		{ "http-connection-close-is-not-reused", []() -> void {
			FakeSocket socket;
			socket.slices = {
				"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2\r\n\r\nab",
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\ncd",
			};
			Memory::Heap<> heap(16384);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			client.Send(request);
			client.ReadBody([](std::span<const uint8>) -> ResultStatus {
				return ResultStatus::ok;
			});
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(socket.openCount, 2);
		} },

		{ "http-unread-body-is-discarded-with-connection", []() -> void {
			FakeSocket socket;
			socket.slices = {
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nab",
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\ncd",
			};
			Memory::Heap<> heap(16384);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			client.Send(request);
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(socket.openCount, 2);
		} },

		{ "http-stale-keepalive-is-retried-once", []() -> void {
			FakeSocket socket;
			socket.slices = {
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nab",
				FakeSocket::CloseMarker,
				"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\ncd",
			};
			Memory::Heap<> heap(16384);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			client.Send(request);
			client.ReadBody([](std::span<const uint8>) -> ResultStatus {
				return ResultStatus::ok;
			});

			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string body;
			client.ReadBody([&body](std::span<const uint8> piece) -> ResultStatus {
				body.append(reinterpret_cast<const char *>(piece.data()), piece.size());
				return ResultStatus::ok;
			});
			VHAL_CHECK_EQ(socket.openCount, 2);
			VHAL_CHECK_EQ(body, "cd");
		} },

		{ "http-timeout-is-reported", []() -> void {
			FakeSocket socket;
			socket.endStatus = ResultStatus::timeout;
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::timeout);
		} },

		{ "http-connect-timeout-separate-from-io", []() -> void {
			FakeSocket socket;
			socket.slices = { "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n" };
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			client.options.connectTimeoutMs = 4000;
			client.options.timeoutMs = 2500;
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);
			VHAL_CHECK_EQ(socket.connectTimeoutAtOpen, 4000);
			VHAL_CHECK(!socket.readTimeouts.empty() && socket.readTimeouts[0] == 2500);
		} },

		{ "http-head-with-body-refused", []() -> void {
			FakeSocket socket;
			Memory::Heap<> heap(8192);
			Memory::AllocatorOf<Memory::Heap<>> memory(heap);
			HttpClient client(socket, memory);
			const uint8 payload[] = { 'x' };
			HttpClient::Request request;
			request.method = Method::Head;
			request.url = "http://example.test/";
			request.body = payload;
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::invalidParameter);
		} },

		{ "http-defragment-during-head-and-body", []() -> void {
			Memory::Heap<Memory::IncrementalDefrag<8>> heap(4096);
			Memory::AllocatorOf<Memory::Heap<Memory::IncrementalDefrag<8>>> memory(heap);
			Memory::Handle gap = heap.Allocate(8);
			Memory::Handle live = heap.Allocate(64);
			heap.Deallocate(gap);
			void *before = live.Data();

			FakeSocket socket;
			socket.slices = {
				"HTTP/1.1 200 OK\r\nX-Pad: ",
				"0123456789",
				"\r\nContent-Length: 12\r\n\r\nabcd",
				"efgh",
				"ijkl",
			};
			socket.beforeRead = [&heap]() -> void {
				heap.Defragment();
			};

			HttpClient client(socket, memory);
			HttpClient::Request request;
			request.url = "http://example.test/";
			VHAL_CHECK_EQ(client.Send(request), ResultStatus::ok);

			std::string body;
			ResultStatus status = client.ReadBody([&body](std::span<const uint8> piece) -> ResultStatus {
				body.append(reinterpret_cast<const char *>(piece.data()), piece.size());
				return ResultStatus::ok;
			});
			VHAL_CHECK_EQ(status, ResultStatus::ok);
			VHAL_CHECK_EQ(body, "abcdefghijkl");

			std::string_view value;
			VHAL_CHECK(client.FindHeader("x-pad", value));
			VHAL_CHECK_EQ(value, "0123456789");
			VHAL_CHECK(live.Data() != before);
		} },
	};

	for (const TestCase &testCase : cases) {
		int before = VhalCheck::failCount;
		testCase.run();
		if (VhalCheck::failCount != before) {
			std::fprintf(stderr, "case failed: %s\n", testCase.name);
		}
	}

	return VhalCheck::Finish("TestHttpUnit");
}
