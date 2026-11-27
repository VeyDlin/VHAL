#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>

#include <array>
#include <span>
#include <string_view>


// Host tests of JSON::Writer with integer literals: plain int, long and unsigned literals keep their integer form, and every integer
// width is written as its value.


void KeyValueIntLiteral() {
	std::array<char, 128> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject()
		.KeyValue("port", 16000)
		.Comma()
		.KeyValue("neg", -5)
		.EndObject();

	VHAL_CHECK_EQ(writer.GetResult(), R"({"port":16000,"neg":-5})");
	VHAL_CHECK(!writer.HasError());
}


void NumberIntLiteral() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.Number(16000);

	VHAL_CHECK_EQ(writer.GetResult(), "16000");
}


void AllIntegerTypes() {
	std::array<char, 256> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject()
		.KeyValue("i", 1)
		.Comma()
		.KeyValue("u", 2u)
		.Comma()
		.KeyValue("l", 3L)
		.Comma()
		.KeyValue("ul", 4UL)
		.Comma()
		.KeyValue("ll", 5LL)
		.Comma()
		.KeyValue("ull", 6ULL)
		.Comma()
		.KeyValue("i8", static_cast<int8>(-8))
		.Comma()
		.KeyValue("u8", static_cast<uint8>(200))
		.Comma()
		.KeyValue("i16", static_cast<int16>(-1600))
		.Comma()
		.KeyValue("u32", static_cast<uint32>(4000000000u))
		.EndObject();

	VHAL_CHECK_EQ(writer.GetResult(), R"({"i":1,"u":2,"l":3,"ul":4,"ll":5,"ull":6,"i8":-8,"u8":200,"i16":-1600,"u32":4000000000})");
	VHAL_CHECK(!writer.HasError());
}


void IntLiteralDoesNotBecomeBool() {
	std::array<char, 64> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.KeyValue("n", 1);

	VHAL_CHECK_EQ(writer.GetResult(), R"("n":1)");
}


int main() {
	KeyValueIntLiteral();
	NumberIntLiteral();
	AllIntegerTypes();
	IntLiteralDoesNotBecomeBool();

	return VhalCheck::Finish("TestJsonWriterLiterals");
}
