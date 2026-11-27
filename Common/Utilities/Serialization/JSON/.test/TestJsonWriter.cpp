#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>


// Host tests of JSON::Writer: the objects, arrays, keys and values, the escapes of strings and keys, the number formats, the
// overflow of the buffer (an error, never a partial token), and the length that matches the result.

// Note: \u sequences are written as ordinary string literals with a doubled backslash (\\u).
// GCC and clang rewrite \u inside raw string literals into UTF-8, which would hide the escape.


void KeyValueCStringWritesString() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject()
		.KeyValue("name", "John")
		.Comma()
		.KeyValue("age", static_cast<int64>(30))
		.Comma()
		.KeyValue("active", true)
		.EndObject();

	VHAL_CHECK_EQ(writer.GetResult(), R"({"name":"John","age":30,"active":true})");
	VHAL_CHECK(!writer.HasError());
}


void StringEscapes() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.String("a\"b\\c\n\t\r\b\f");

	VHAL_CHECK_EQ(writer.GetResult(), R"("a\"b\\c\n\t\r\b\f")");
}


void ControlCharsAreUnicodeEscaped() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.String(std::string_view("\x01\x1f", 2));

	VHAL_CHECK_EQ(writer.GetResult(), "\"\\u0001\\u001f\"");
}


void Utf8PassesThroughUnchanged() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.String("Привет");

	VHAL_CHECK_EQ(writer.GetResult(), "\"Привет\"");
	VHAL_CHECK(!writer.HasError());
}


void KeyIsEscaped() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject().Key("a\"b").Number(static_cast<int64>(1)).EndObject();

	VHAL_CHECK_EQ(writer.GetResult(), R"({"a\"b":1})");
}


void OverflowSetsError() {
	std::array<char, 8> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.String("0123456789");

	VHAL_CHECK(writer.HasError());
}


void DoubleFormatting() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartArray()
		.Number(3.14159, 2)
		.Comma()
		.Number(25.5)
		.EndArray();

	VHAL_CHECK_EQ(writer.GetResult(), "[3.14,25.500000]");
	VHAL_CHECK(!writer.HasError());
}


void NonFiniteDoubleIsError() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.Number(std::nan(""));

	VHAL_CHECK(writer.HasError());
}


void NumberInt64Negative() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.Number(static_cast<int64>(-42));

	VHAL_CHECK_EQ(writer.GetResult(), "-42");
}


void WikiQuickStart() {
	std::array<char, 256> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject()
		.KeyValue("name", "John")
		.Comma()
		.KeyValue("age", static_cast<int64>(30))
		.Comma()
		.KeyValue("active", true)
		.EndObject();

	VHAL_CHECK_EQ(writer.GetResult(), R"({"name":"John","age":30,"active":true})");
}


void WikiNested() {
	std::array<char, 512> buffer{};
	JSON::Writer w{std::span(buffer)};

	w.StartObject()
		.Key("user").StartObject()
			.KeyValue("id", static_cast<int64>(123))
			.Comma()
			.KeyValue("email", "john@example.com")
		.EndObject()
		.Comma()
		.Key("tags").StartArray()
			.String("dark_mode")
			.Comma()
			.String("notifications")
		.EndArray()
		.EndObject();

	VHAL_CHECK_EQ(w.GetResult(), R"({"user":{"id":123,"email":"john@example.com"},"tags":["dark_mode","notifications"]})");
}


void WikiNumberFormatting() {
	std::array<char, 256> buffer{};
	JSON::Writer w{std::span(buffer)};

	w.StartObject()
		.Key("int_val").Number(static_cast<int64>(-42))
		.Comma()
		.Key("uint_val").Number(static_cast<uint64>(100))
		.Comma()
		.Key("float_default").Number(3.14159)
		.Comma()
		.Key("float_precise").Number(3.14159, 2)
		.EndObject();

	VHAL_CHECK_EQ(w.GetResult(), R"({"int_val":-42,"uint_val":100,"float_default":3.141590,"float_precise":3.14})");
}


void WikiErrorHandling() {
	std::array<char, 10> smallBuffer{};
	JSON::Writer w{std::span(smallBuffer)};

	w.StartObject()
		.KeyValue("long_key_name", "long_value")
		.EndObject();

	VHAL_CHECK(w.HasError());
}


void LengthMatchesResult() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject().KeyValue("k", "v").EndObject();

	VHAL_CHECK_EQ(writer.GetLength(), writer.GetResult().size());
	VHAL_CHECK_EQ(writer.GetResult(), R"({"k":"v"})");
}


int main() {
	KeyValueCStringWritesString();
	StringEscapes();
	ControlCharsAreUnicodeEscaped();
	Utf8PassesThroughUnchanged();
	KeyIsEscaped();
	OverflowSetsError();
	DoubleFormatting();
	NonFiniteDoubleIsError();
	NumberInt64Negative();
	WikiQuickStart();
	WikiNested();
	WikiNumberFormatting();
	WikiErrorHandling();
	LengthMatchesResult();

	return VhalCheck::Finish("TestJsonWriter");
}
