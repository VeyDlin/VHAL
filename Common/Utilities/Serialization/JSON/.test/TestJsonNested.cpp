#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>

#include <array>
#include <span>
#include <string>
#include <string_view>


// Host tests of JSON::GetRaw, GetArrayElement, GetString, GetBool and GetNumber on nested documents: streaming chunks with raw and
// \u-escaped UTF-8, exact substrings with inner whitespace and escapes, array indexing, bad input, and a nesting depth of 5000 that must
// not recurse. The sample documents are neutral: their keys and shapes follow a streaming chat response and a transcript event.


// Sample streaming transcript event, raw UTF-8.
static constexpr std::string_view sampleTranscriptPartial = R"JSON({"type":"sample.partial","text":"Привет, мир, как дела?","words":[{"text":"Привет,","start":0.021,"end":0.442},{"text":"мир","start":0.784,"end":1.105}],"is_final":true,"speech_final":false,"start":0.001,"duration":2.549,"language":"ru"})JSON";

// Sample streaming chat chunk, raw UTF-8.
static constexpr std::string_view sampleChatChunkRawUtf8 = R"JSON({"id":"3f2a9c1e-5b7d-4e8a-9c2f-1a6b3d4e5f60","object":"example.chunk","created":1700000000,"model":"sample-model-1.0-standard","choices":[{"index":0,"delta":{"content":"При","role":"assistant"}}],"system_fingerprint":"fp_0123456789","service_tier":"default"})JSON";

// Sample chat response with one choice, raw UTF-8.
static constexpr std::string_view sampleChatRaw = R"JSON({"choices":[{"index":0,"delta":{"content":"Пример"}}]})JSON";


namespace {


// Replaces the first occurrence of 'from' with 'to'. Used to derive the \u-escaped variants.
std::string ReplaceFirst(std::string_view text, std::string_view from, std::string_view to) {
	std::string result(text);
	size_t pos = result.find(from);
	if (pos != std::string::npos) {
		result.replace(pos, from.size(), to);
	}
	return result;
}


} // namespace


void TranscriptPartialRawUtf8() {
	std::array<char, 128> buffer{};
	size_t length = 0;

	VHAL_CHECK(JSON::GetString(sampleTranscriptPartial, "type", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "sample.partial");

	VHAL_CHECK(JSON::GetString(sampleTranscriptPartial, "text", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "Привет, мир, как дела?");

	bool isFinal = false;
	bool speechFinal = true;
	VHAL_CHECK(JSON::GetBool(sampleTranscriptPartial, "is_final", isFinal));
	VHAL_CHECK(isFinal);
	VHAL_CHECK(JSON::GetBool(sampleTranscriptPartial, "speech_final", speechFinal));
	VHAL_CHECK(!speechFinal);

	double duration = 0.0;
	VHAL_CHECK(JSON::GetNumber(sampleTranscriptPartial, "duration", duration));
	VHAL_CHECK_NEAR(duration, 2.549, 1e-9 * 2.549);

	// Top-level "start" must not be confused with the "start" fields inside "words".
	double start = 0.0;
	VHAL_CHECK(JSON::GetNumber(sampleTranscriptPartial, "start", start));
	VHAL_CHECK_NEAR(start, 0.001, 1e-9);

	VHAL_CHECK(JSON::GetString(sampleTranscriptPartial, "language", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "ru");
}


void TranscriptWordsByIndex() {
	std::array<char, 64> buffer{};
	size_t length = 0;

	std::string_view words = JSON::GetRaw(sampleTranscriptPartial, "words");
	VHAL_CHECK(!words.empty());

	std::string_view second = JSON::GetArrayElement(words, 1);
	VHAL_CHECK(JSON::GetString(second, "text", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "мир");

	double end = 0.0;
	VHAL_CHECK(JSON::GetNumber(second, "end", end));
	VHAL_CHECK_NEAR(end, 1.105, 1e-9 * 1.105);

	VHAL_CHECK(JSON::GetArrayElement(words, 2).empty());
}


void ChatChunkRawUtf8Chain() {
	std::array<char, 32> buffer{};
	size_t length = 0;

	std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(sampleChatChunkRawUtf8, "choices"), 0);
	VHAL_CHECK(!choice.empty());

	std::string_view delta = JSON::GetRaw(choice, "delta");
	VHAL_CHECK(JSON::GetString(delta, "content", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "При");

	VHAL_CHECK(JSON::GetString(delta, "role", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "assistant");

	uint64 index = 99;
	VHAL_CHECK(JSON::GetNumber(choice, "index", index));
	VHAL_CHECK_EQ(index, 0);

	uint64 created = 0;
	VHAL_CHECK(JSON::GetNumber(sampleChatChunkRawUtf8, "created", created));
	VHAL_CHECK_EQ(created, 1700000000ULL);
}


void ChatChunkEscapedChain() {
	std::array<char, 32> buffer{};
	size_t length = 0;

	// Same chunk as the raw sample with the content re-encoded as \u escapes (ASCII-only encoders do this).
	const std::string escaped = ReplaceFirst(sampleChatChunkRawUtf8, "При", "\\u041f\\u0440\\u0438");
	VHAL_CHECK(escaped.find("\\u041f") != std::string::npos);

	std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(escaped, "choices"), 0);
	std::string_view delta = JSON::GetRaw(choice, "delta");
	VHAL_CHECK(JSON::GetString(delta, "content", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "При");
}


void ChatSampleRawAndEscaped() {
	std::array<char, 32> buffer{};
	size_t length = 0;

	const std::string escaped = ReplaceFirst(sampleChatRaw, "Пример", "\\u041f\\u0440\\u0438\\u043c\\u0435\\u0440");
	const std::string raw(sampleChatRaw);

	for (const std::string& json : {raw, escaped}) {
		std::string_view choice = JSON::GetArrayElement(JSON::GetRaw(json, "choices"), 0);
		std::string_view delta = JSON::GetRaw(choice, "delta");
		VHAL_CHECK(JSON::GetString(delta, "content", std::span(buffer), length));
		VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "Пример");
	}
}


void GetRawExactSubstring() {
	const std::string_view json = R"({"a":{"x":[1,2,{"y":"]}\""}]},"b":2})";

	VHAL_CHECK_EQ(JSON::GetRaw(json, "a"), R"({"x":[1,2,{"y":"]}\""}]})");
	VHAL_CHECK_EQ(JSON::GetRaw(json, "b"), "2");
	VHAL_CHECK(JSON::GetRaw(json, "missing").empty());
}


void GetRawKeepsWhitespaceInsideValue() {
	const std::string_view json = "{ \"a\" : [ 1 , { \"b\" : \"c\" } ] }\n";

	VHAL_CHECK_EQ(JSON::GetRaw(json, "a"), "[ 1 , { \"b\" : \"c\" } ]");
}


void GetArrayElementIndexing() {
	const std::string_view array = R"([10,"s",{"k":1},[2,3],true,null])";

	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 0), "10");
	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 1), R"("s")");
	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 2), R"({"k":1})");
	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 3), "[2,3]");
	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 4), "true");
	VHAL_CHECK_EQ(JSON::GetArrayElement(array, 5), "null");
	VHAL_CHECK(JSON::GetArrayElement(array, 6).empty());
}


void GetArrayElementInvalidInputs() {
	VHAL_CHECK(JSON::GetArrayElement("[]", 0).empty());
	VHAL_CHECK(JSON::GetArrayElement(R"({"a":1})", 0).empty());
	VHAL_CHECK(JSON::GetArrayElement(R"("abc")", 0).empty());
	VHAL_CHECK(JSON::GetArrayElement("[1,]", 1).empty());
	VHAL_CHECK(JSON::GetArrayElement("[1 2]", 1).empty());
	VHAL_CHECK(JSON::GetArrayElement("[1,2", 1).empty());
}


void GetBoolValues() {
	bool value = false;

	VHAL_CHECK(JSON::GetBool(R"({"t":true,"f":false})", "t", value));
	VHAL_CHECK(value);
	VHAL_CHECK(JSON::GetBool(R"({"t":true,"f":false})", "f", value));
	VHAL_CHECK(!value);

	VHAL_CHECK(!JSON::GetBool(R"({"t":true})", "missing", value));
	VHAL_CHECK(!JSON::GetBool(R"({"s":"true"})", "s", value));
	VHAL_CHECK(!JSON::GetBool(R"({"n":1})", "n", value));
	VHAL_CHECK(!JSON::GetBool(R"({"x":truex})", "x", value));
}


void DeepNestingDoesNotRecurse() {
	constexpr size_t depth = 5000;
	std::string json(depth, '[');
	json += "1";
	json.append(depth, ']');

	// Element 0 of the outer array is the inner array; its text is everything between the outer brackets.
	std::string_view inner = JSON::GetArrayElement(json, 0);
	VHAL_CHECK_EQ(inner.size(), 2 * (depth - 1) + 1);

	std::string deepObject = R"({"d":)" + json + "}";
	VHAL_CHECK_EQ(JSON::GetRaw(deepObject, "d").size(), json.size());
}


void UnterminatedCompositeIsNotFound() {
	VHAL_CHECK(JSON::GetRaw(R"({"a":[1,2)", "a").empty());
	VHAL_CHECK(JSON::GetRaw(R"({"a":"abc)", "a").empty());
}


void MalformedSeparatorsAreNotFound() {
	double value = 0.0;

	VHAL_CHECK(!JSON::GetNumber(R"({"a":1 "b":2})", "b", value));
	VHAL_CHECK(!JSON::GetNumber(R"({"a":1,})", "b", value));
}


int main() {
	TranscriptPartialRawUtf8();
	TranscriptWordsByIndex();
	ChatChunkRawUtf8Chain();
	ChatChunkEscapedChain();
	ChatSampleRawAndEscaped();
	GetRawExactSubstring();
	GetRawKeepsWhitespaceInsideValue();
	GetArrayElementIndexing();
	GetArrayElementInvalidInputs();
	GetBoolValues();
	DeepNestingDoesNotRecurse();
	UnterminatedCompositeIsNotFound();
	MalformedSeparatorsAreNotFound();

	return VhalCheck::Finish("TestJsonNested");
}
