// Unit tests for ServerSentEvents, following the WHATWG "Server-sent events" rules.
#include <TestHeap.h>
#include <Utilities/Callback/Delegate.h>
#include <VhalCheck.h>
#include <Drivers/Interface/Network/Http/ServerSentEvents.h>
#include <Utilities/Memory/Heap.h>
#include <cstdio>
#include <span>
#include <string>
#include <vector>


namespace {

struct Event {
	std::string type;
	std::string data;
	std::string id;
};


// Stores the events that a parser dispatches. The parser reaches OnEvent through a delegate bound to the object.
class Recorder {
public:
	std::vector<Event> events;

	void OnEvent(std::string_view type, std::string_view data, std::string_view id) {
		events.push_back(Event{ std::string(type), std::string(data), std::string(id) });
	}
};


using EventDelegate = Callback::Delegate<void(std::string_view, std::string_view, std::string_view)>;


// Collects the events of one parser. The heap is part of the fixture so each test owns its memory.
class Collector : public Recorder {
public:
	TestHeap<> heap;
	ServerSentEvents parser;

	explicit Collector(uint32 maxEventBytes = 65536)
		: heap(65536),
		  parser(heap, maxEventBytes) {
		parser.onEvent = EventDelegate::Bind<Recorder, &Recorder::OnEvent>(this);
	}


	ResultStatus Feed(std::string_view text) {
		return parser.Feed(std::span<const uint8>(reinterpret_cast<const uint8 *>(text.data()), text.size()));
	}
};


const char *FffdText = "\xEF\xBF\xBD";

const char *ChunkStream =
	"data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Привет\"}}]}\n\n"
	"data: [END]\n\n";


std::vector<Event> Parse(const std::string& stream) {
	Collector collector;
	collector.Feed(stream);
	return collector.events;
}


// Parses stream with a feed boundary after every cut point.
std::vector<Event> ParseCut(const std::string& stream, const std::vector<std::size_t>& cuts) {
	Collector collector;
	std::size_t start = 0;
	for (std::size_t cut : cuts) {
		collector.Feed(std::string_view(stream).substr(start, cut - start));
		start = cut;
	}

	collector.Feed(std::string_view(stream).substr(start));
	return collector.events;
}


// Every event matches its expected type, data and id, and the counts are equal.
void ExpectEvents(const std::vector<Event>& actual, const std::vector<Event>& expected) {
	VHAL_CHECK_EQ(actual.size(), expected.size());
	for (std::size_t index = 0; index < actual.size() && index < expected.size(); index++) {
		VHAL_CHECK_EQ(actual[index].type, expected[index].type);
		VHAL_CHECK_EQ(actual[index].data, expected[index].data);
		VHAL_CHECK_EQ(actual[index].id, expected[index].id);
	}
}


void CheckEvents(const std::string& stream, const std::vector<Event>& expected) {
	ExpectEvents(Parse(stream), expected);
}


// Every split of stream into two feeds must give the same events as one feed.
void CheckEverySplit(const std::string& stream) {
	std::vector<Event> whole = Parse(stream);
	for (std::size_t cut = 1; cut < stream.size(); cut++) {
		ExpectEvents(ParseCut(stream, { cut }), whole);
	}

	std::vector<std::size_t> everyByte;
	for (std::size_t index = 1; index < stream.size(); index++) {
		everyByte.push_back(index);
	}

	ExpectEvents(ParseCut(stream, everyByte), whole);
}


void ChunkStreamEverySplit() {
	CheckEverySplit(ChunkStream);
}

}


int main() {
	const struct TestCase {
		const char *name;
		void (*run)();
	} cases[] = {
		{ "sse-basic-event", []() -> void {
			CheckEvents("data: hello\n\n", { Event{ "message", "hello", "" } });
		} },

		{ "sse-line-endings-lf-cr-crlf", []() -> void {
			CheckEvents("data: a\n\n", { Event{ "message", "a", "" } });
			CheckEvents("data: a\r\r", { Event{ "message", "a", "" } });
			CheckEvents("data: a\r\n\r\n", { Event{ "message", "a", "" } });
			CheckEvents("data: x\r\ndata: y\rdata: z\n\n", { Event{ "message", "x\ny\nz", "" } });
		} },

		{ "sse-crlf-split-across-feeds", []() -> void {
			Collector collector;
			collector.Feed("data: a\r");
			collector.Feed("\n\r");
			collector.Feed("\n");
			ExpectEvents(collector.events, { Event{ "message", "a", "" } });
		} },

		{ "sse-bom-at-start-is-removed", []() -> void {
			std::string stream = std::string("\xEF\xBB\xBF") + "data: a\n\n";
			CheckEvents(stream, { Event{ "message", "a", "" } });
			ExpectEvents(ParseCut(stream, { 1, 2 }), { Event{ "message", "a", "" } });
		} },

		{ "sse-bom-in-value-is-kept", []() -> void {
			std::string bom = "\xEF\xBB\xBF";
			CheckEvents("data: " + bom + "a\n\n", { Event{ "message", bom + "a", "" } });
		} },

		{ "sse-comments-ignored", []() -> void {
			CheckEvents(": keep-alive\n:\ndata: a\n\n", { Event{ "message", "a", "" } });
		} },

		{ "sse-multi-line-data", []() -> void {
			CheckEvents("data: first\ndata: second\n\n", { Event{ "message", "first\nsecond", "" } });
		} },

		{ "sse-event-type-and-id-persist", []() -> void {
			CheckEvents("event: update\nid: 7\ndata: one\n\ndata: two\n\n",
				{ Event{ "update", "one", "7" }, Event{ "message", "two", "7" } });
		} },

		{ "sse-id-with-nul-is-ignored", []() -> void {
			std::string stream = std::string("id: 5\n\n") + std::string("id: 9\0x", 7) + "\ndata: b\n\n";
			CheckEvents(stream, { Event{ "message", "b", "5" } });
		} },

		{ "sse-retry-parsing", []() -> void {
			Collector collector;
			collector.Feed("retry: 3000\ndata: a\n\n");
			VHAL_CHECK(collector.parser.HasRetry());
			VHAL_CHECK_EQ(collector.parser.GetRetry(), 3000);

			collector.Feed("retry: 3s\nretry:\n\n");
			VHAL_CHECK_EQ(collector.parser.GetRetry(), 3000);

			Collector huge;
			huge.Feed("retry: 99999999999999999999\n\n");
			VHAL_CHECK_EQ(huge.parser.GetRetry(), 0xFFFFFFFFu);
		} },

		{ "sse-field-without-colon", []() -> void {
			CheckEvents("data\n\n", { Event{ "message", "", "" } });
		} },

		{ "sse-empty-data-not-dispatched", []() -> void {
			Collector collector;
			collector.Feed("\n\nevent: x\n\n");
			ExpectEvents(collector.events, {});

			collector.Feed("data: y\n\n");
			ExpectEvents(collector.events, { Event{ "message", "y", "" } });
		} },

		{ "sse-unterminated-event-not-dispatched", []() -> void {
			CheckEvents("data: x\n", {});
		} },

		{ "sse-unknown-field-ignored", []() -> void {
			CheckEvents("foo: bar\ndata: 1\n\n", { Event{ "message", "1", "" } });
		} },

		{ "sse-single-leading-space-only", []() -> void {
			CheckEvents("data:x\n\ndata:  y\n\n",
				{ Event{ "message", "x", "" }, Event{ "message", " y", "" } });
		} },

		{ "sse-chunk-stream-every-split", ChunkStreamEverySplit },

		{ "sse-chunk-stream-expected-events", []() -> void {
			CheckEvents(ChunkStream, {
				Event{ "message", "{\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Привет\"}}]}", "" },
				Event{ "message", "[END]", "" },
			});
		} },

		{ "sse-utf8-split-inside-character", []() -> void {
			// "П" is D0 9F: cut between its two bytes.
			Collector collector;
			collector.Feed("data: \xD0");
			collector.Feed("\x9F" "ривет\n\n");
			ExpectEvents(collector.events, { Event{ "message", "Привет", "" } });
		} },

		{ "sse-invalid-utf8-becomes-replacement", []() -> void {
			CheckEvents("data: \xFF\n\n", { Event{ "message", FffdText, "" } });
			CheckEvents("data: \xD0\n\n", { Event{ "message", FffdText, "" } });
			CheckEvents("data: \xC0\xAF\n\n", { Event{ "message", std::string(FffdText) + FffdText, "" } });
			CheckEvents("data: \xED\xA0\x80\n\n", { Event{ "message", std::string(FffdText) + FffdText + FffdText, "" } });
		} },

		{ "sse-max-size-drops-event-and-resyncs", []() -> void {
			Collector collector(16);
			ResultStatus status = collector.Feed("data: aaaaaaaaaaaaaaaaaaaaaaaa\n\ndata: ok\n\n");
			VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
			ExpectEvents(collector.events, { Event{ "message", "ok", "" } });
		} },

		{ "sse-max-size-drop-mid-event-resyncs", []() -> void {
			Collector collector(10);
			ResultStatus status = collector.Feed("data: abcd\ndata: efgh\ndata: ij\n\ndata: z\n\n");
			VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
			ExpectEvents(collector.events, { Event{ "message", "z", "" } });
		} },

		{ "sse-max-size-type-too-long-dropped", []() -> void {
			Collector collector(8);
			ResultStatus status = collector.Feed("event: longtypename\ndata: x\n\ndata: ok\n\n");
			VHAL_CHECK_EQ(status, ResultStatus::outOfRange);
			ExpectEvents(collector.events, { Event{ "message", "ok", "" } });
		} },

		{ "sse-dropped-event-still-updates-id", []() -> void {
			Collector collector(16);
			collector.Feed("id: 4\ndata: ");
			collector.Feed(std::string(30, 'a'));
			collector.Feed("\n\ndata: z\n\n");
			ExpectEvents(collector.events, { Event{ "message", "z", "4" } });
		} },

		{ "sse-reset-forgets-last-id", []() -> void {
			Collector collector;
			collector.Feed("id: 3\ndata: a\n\n");
			collector.parser.Reset();
			collector.Feed("data: b\n\n");
			ExpectEvents(collector.events, { Event{ "message", "a", "3" }, Event{ "message", "b", "" } });
		} },

		{ "sse-feed-ok-for-normal-stream", []() -> void {
			Collector collector;
			ResultStatus status = collector.Feed(ChunkStream);
			VHAL_CHECK_EQ(status, ResultStatus::ok);
		} },

		{ "sse-heap-defragments-between-feeds", []() -> void {
			TestHeap<Memory::IncrementalDefrag<8>> heap(8192);
			Memory::Handle gap = heap.Allocate(8);
			Memory::Handle live = heap.Allocate(64);
			heap.Deallocate(gap);
			void *before = live.Data();

			ServerSentEvents parser(heap, 4096);
			Recorder recorder;
			parser.onEvent = EventDelegate::Bind<Recorder, &Recorder::OnEvent>(&recorder);

			std::string stream =
				"id: 12\n"
				"event: chunk\n"
				"data: first line of a long event\n"
				"data: second line, arriving byte by byte\n"
				"data: третья строка\n"
				"\n"
				"data: [END]\n"
				"\n";
			for (std::size_t index = 0; index < stream.size(); index++) {
				if (index % 3 == 0) {
					heap.Defragment();
				}

				parser.Feed(std::span<const uint8>(reinterpret_cast<const uint8 *>(stream.data()) + index, 1));
			}

			ExpectEvents(recorder.events, {
				Event{ "chunk", "first line of a long event\nsecond line, arriving byte by byte\nтретья строка", "12" },
				Event{ "message", "[END]", "12" },
			});
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

	return VhalCheck::Finish("TestSse");
}
