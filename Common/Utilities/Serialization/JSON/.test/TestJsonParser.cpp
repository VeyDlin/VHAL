#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>

#include <array>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>


// Host tests of JSON::Parser and of JSON::GetString, GetNumber and FindValue: the string escapes (simple, \u and surrogate pairs,
// raw UTF-8), the output limits, the number tokens and their ranges, the lookup of keys, and skipping of values.

// Inputs with \u escapes use ordinary string literals and a doubled backslash (\\u); raw string
// literals would have the \u rewritten to UTF-8 by the compiler before the parser sees it.


namespace {


bool ParseStringInput(std::string_view input, std::span<char> output, size_t& length) {
	JSON::Parser parser(input);
	return parser.ParseString(output, length);
}


// The tolerance of a floating-point check: relative to the expected value, and at least 1e-9.
double Tolerance(double expected) {
	return 1e-9 * std::fmax(1.0, std::fabs(expected));
}


} // namespace


void ParserStringSimpleEscapes() {
	std::array<char, 64> buffer{};
	size_t length = 0;

	VHAL_CHECK(ParseStringInput(R"("a\"b\\c\/d\b\f\n\r\t")", std::span(buffer), length));

	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "a\"b\\c/d\b\f\n\r\t");
}


void ParserUnicodeCyrillicEscapes() {
	std::array<char, 64> buffer{};
	size_t length = 0;

	VHAL_CHECK(ParseStringInput("\"\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442\"", std::span(buffer), length));

	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "Привет");
}


void ParserUnicodeSurrogatePair() {
	std::array<char, 16> buffer{};
	size_t length = 0;

	VHAL_CHECK(ParseStringInput("\"\\ud83d\\ude00\"", std::span(buffer), length));

	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "\xF0\x9F\x98\x80");
}


void ParserRawUtf8PassesThrough() {
	std::array<char, 64> buffer{};
	size_t length = 0;

	VHAL_CHECK(ParseStringInput("\"Привет\"", std::span(buffer), length));

	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "Привет");
}


void ParserRejectsInvalidEscapes() {
	const std::vector<std::string> invalidInputs = {
		"\"\\uZZZZ\"",
		"\"\\u12\"",
		"\"\\ud83d\"",
		"\"\\ude00\"",
		"\"\\ud83d\\u0041\"",
		"\"\\x41\"",
		"\"abc\\",
	};

	for (const std::string& input : invalidInputs) {
		std::array<char, 64> buffer{};
		size_t length = 0;
		VHAL_CHECK(!ParseStringInput(input, std::span(buffer), length));
	}
}


void ParserOutputOverflowIsError() {
	std::array<char, 5> buffer{};
	size_t length = 0;

	VHAL_CHECK(!ParseStringInput(R"("abcdef")", std::span(buffer), length));
	VHAL_CHECK_EQ(length, 0);
}


void ParserExactFitAccepted() {
	std::array<char, 6> buffer{};
	size_t length = 0;

	VHAL_CHECK(ParseStringInput(R"("abcdef")", std::span(buffer), length));
	VHAL_CHECK_EQ(length, 6);
}


void ParserCodePointNotSplit() {
	std::array<char, 3> buffer{};
	size_t length = 0;

	VHAL_CHECK(!ParseStringInput("\"\\ud83d\\ude00\"", std::span(buffer), length));
}


void ParserUnterminatedStringFails() {
	std::array<char, 16> buffer{};
	size_t length = 0;

	VHAL_CHECK(!ParseStringInput(R"("abc)", std::span(buffer), length));
	VHAL_CHECK(!ParseStringInput(R"("abc\")", std::span(buffer), length));
}


void ParserRawControlCharRejected() {
	std::array<char, 16> buffer{};
	size_t length = 0;

	VHAL_CHECK(!ParseStringInput("\"a\nb\"", std::span(buffer), length));
}


void ParserNumberIntegerTokens() {
	int64 value = 99;

	JSON::Parser p42("42");
	VHAL_CHECK(p42.ParseNumber(value));
	VHAL_CHECK_EQ(value, 42);

	JSON::Parser pNeg("-7");
	VHAL_CHECK(pNeg.ParseNumber(value));
	VHAL_CHECK_EQ(value, -7);

	JSON::Parser pZero("0");
	VHAL_CHECK(pZero.ParseNumber(value));
	VHAL_CHECK_EQ(value, 0);

	JSON::Parser pNegZero("-0");
	VHAL_CHECK(pNegZero.ParseNumber(value));
	VHAL_CHECK_EQ(value, 0);
}


void ParserNumberRejectsNonIntegerTokens() {
	const std::vector<std::string_view> invalidInputs = {"1.5", "1e3", "+1", "01", "-", "1.", "1e", "", "abc"};

	for (std::string_view input : invalidInputs) {
		JSON::Parser parser(input);
		int64 value = 0;
		VHAL_CHECK(!parser.ParseNumber(value));
	}
}


void ParserNumberNoOverreadPastView() {
	// Exact-size heap allocation without a NUL terminator: any read past the view is a heap overflow under ASan.
	std::vector<char> exact = {'4', '2'};
	JSON::Parser parser(std::string_view(exact.data(), exact.size()));
	int64 value = 0;

	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK_EQ(value, 42);
	VHAL_CHECK(parser.IsAtEnd());
}


void ParserNumberDouble() {
	double value = 0.0;

	JSON::Parser pExp("1e5");
	VHAL_CHECK(pExp.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 100000.0, Tolerance(100000.0));

	JSON::Parser pNeg("-0.5");
	VHAL_CHECK(pNeg.ParseNumber(value));
	VHAL_CHECK_NEAR(value, -0.5, Tolerance(-0.5));

	JSON::Parser pPi("3.14159");
	VHAL_CHECK(pPi.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 3.14159, Tolerance(3.14159));

	JSON::Parser pSmall("2.5E-3");
	VHAL_CHECK(pSmall.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 0.0025, Tolerance(0.0025));

	JSON::Parser pPlus("+1.0");
	VHAL_CHECK(!pPlus.ParseNumber(value));
}


void ParserNumberDoubleOverflowRejected() {
	double value = 1.5;

	JSON::Parser pHuge("1e400");
	VHAL_CHECK(!pHuge.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 1.5, Tolerance(1.5));

	JSON::Parser pNegHuge("-1e400");
	VHAL_CHECK(!pNegHuge.ParseNumber(value));

	JSON::Parser pLarge("1e308");
	VHAL_CHECK(pLarge.ParseNumber(value));
	VHAL_CHECK(value > 1e307);
}


void ParserUint64Bounds() {
	uint64 value = 0;

	JSON::Parser pMax("18446744073709551615");
	VHAL_CHECK(pMax.ParseNumber(value));
	VHAL_CHECK_EQ(value, 18446744073709551615ULL);

	JSON::Parser pNeg("-1");
	VHAL_CHECK(!pNeg.ParseNumber(value));

	JSON::Parser pOver("18446744073709551616");
	VHAL_CHECK(!pOver.ParseNumber(value));
}


void ParserInt64Bounds() {
	int64 value = 0;

	JSON::Parser pMax("9223372036854775807");
	VHAL_CHECK(pMax.ParseNumber(value));
	VHAL_CHECK_EQ(value, 9223372036854775807LL);

	JSON::Parser pOver("9223372036854775808");
	VHAL_CHECK(!pOver.ParseNumber(value));
}


void GetStringEmptyValue() {
	std::array<char, 8> buffer{};
	size_t length = 99;

	VHAL_CHECK(JSON::GetString(R"({"a":""})", "a", std::span(buffer), length));
	VHAL_CHECK_EQ(length, 0);
}


void GetStringLongValueNotTruncated() {
	std::string value(300, 'x');
	std::string json = R"({"a":")" + value + R"("})";
	std::array<char, 512> buffer{};
	size_t length = 0;

	VHAL_CHECK(JSON::GetString(json, "a", std::span(buffer), length));
	VHAL_CHECK_EQ(length, 300);
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), value);
}


void GetStringTooSmallOutputFails() {
	std::array<char, 10> buffer{};
	size_t length = 0;

	VHAL_CHECK(!JSON::GetString(R"({"a":"0123456789012345678901234567890123456789"})", "a", std::span(buffer), length));
	VHAL_CHECK_EQ(length, 0);
}


void GetStringDecodesUnicodeEscapes() {
	std::array<char, 64> buffer{};
	size_t length = 0;

	const std::string json = "{\"city\":\"\\u041c\\u043e\\u0441\\u043a\\u0432\\u0430\"}";
	VHAL_CHECK(JSON::GetString(json, "city", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "Москва");
}


void GetStringNotFoundAndNonObject() {
	std::array<char, 16> buffer{};
	size_t length = 0;

	VHAL_CHECK(!JSON::GetString(R"({"a":"x"})", "b", std::span(buffer), length));
	VHAL_CHECK(!JSON::GetString(R"(["a"])", "a", std::span(buffer), length));
}


void GetStringKeyWithEscape() {
	std::array<char, 16> buffer{};
	size_t length = 0;

	const std::string json = "{\"na\\u006de\":\"x\"}";
	VHAL_CHECK(JSON::GetString(json, "name", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "x");
}


void KeyLongerThan64Matches() {
	std::string key(100, 'k');
	std::string json = R"({")" + key + R"(":"v"})";
	std::array<char, 8> buffer{};
	size_t length = 0;

	VHAL_CHECK(JSON::GetString(json, key, std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "v");
}


void FirstDuplicateKeyWins() {
	std::array<char, 8> buffer{};
	size_t length = 0;

	VHAL_CHECK(JSON::GetString(R"({"a":"1","a":"2"})", "a", std::span(buffer), length));
	VHAL_CHECK_EQ(std::string_view(buffer.data(), length), "1");
}


void GetNumberScientificDouble() {
	double value = 0.0;

	VHAL_CHECK(JSON::GetNumber(R"({"x":1e5})", "x", value));
	VHAL_CHECK_NEAR(value, 100000.0, Tolerance(100000.0));
}


void GetNumberIntRejectsFraction() {
	int64 value = 0;

	VHAL_CHECK(!JSON::GetNumber(R"({"x":25.5})", "x", value));
}


void GetNumberUintRejectsNegative() {
	uint64 value = 0;

	VHAL_CHECK(!JSON::GetNumber(R"({"x":-1})", "x", value));
}


void GetNumberOnStringValueFails() {
	double value = 0.0;

	VHAL_CHECK(!JSON::GetNumber(R"({"x":"12"})", "x", value));
}


void FindValueBooleanAndObjectReturnRaw() {
	std::array<char, 16> valueBuffer{};
	JSON::Parser parser(R"({"flag":true,"obj":{"a":1}})");

	VHAL_CHECK_EQ(parser.FindValue("flag", std::span(valueBuffer)), "true");
	VHAL_CHECK_EQ(parser.FindValue("obj", std::span(valueBuffer)), R"({"a":1})");
}


void SkipValueSkipsWholeObject() {
	JSON::Parser parser(R"({"a":[1,{"b":"}"}]}  )");

	parser.SkipValue();

	VHAL_CHECK(parser.IsAtEnd());
}


void SkipValueUnterminatedConsumesInput() {
	JSON::Parser parser(R"({"a":"abc)");

	parser.SkipValue();

	VHAL_CHECK(parser.IsAtEnd());
}


void ParserValueTypes() {
	JSON::Parser pTrue("true");
	VHAL_CHECK(pTrue.GetValueType() == JSON::Type::BOOLEAN);

	JSON::Parser pNull("null");
	VHAL_CHECK(pNull.GetValueType() == JSON::Type::NULL_TYPE);

	JSON::Parser pStr(R"("s")");
	VHAL_CHECK(pStr.GetValueType() == JSON::Type::STRING);

	JSON::Parser pNum("-1");
	VHAL_CHECK(pNum.GetValueType() == JSON::Type::NUMBER);

	JSON::Parser pObj("{}");
	VHAL_CHECK(pObj.GetValueType() == JSON::Type::OBJECT);

	JSON::Parser pArr("[]");
	VHAL_CHECK(pArr.GetValueType() == JSON::Type::ARRAY);
}


void ParserBooleanAndNull() {
	bool value = false;

	JSON::Parser pTrue("true");
	VHAL_CHECK(pTrue.ParseBoolean(value));
	VHAL_CHECK(value);

	JSON::Parser pFalse("false");
	VHAL_CHECK(pFalse.ParseBoolean(value));
	VHAL_CHECK(!value);

	JSON::Parser pNull("null");
	VHAL_CHECK(pNull.ParseNull());
}


int main() {
	ParserStringSimpleEscapes();
	ParserUnicodeCyrillicEscapes();
	ParserUnicodeSurrogatePair();
	ParserRawUtf8PassesThrough();
	ParserRejectsInvalidEscapes();
	ParserOutputOverflowIsError();
	ParserExactFitAccepted();
	ParserCodePointNotSplit();
	ParserUnterminatedStringFails();
	ParserRawControlCharRejected();
	ParserNumberIntegerTokens();
	ParserNumberRejectsNonIntegerTokens();
	ParserNumberNoOverreadPastView();
	ParserNumberDouble();
	ParserNumberDoubleOverflowRejected();
	ParserUint64Bounds();
	ParserInt64Bounds();
	GetStringEmptyValue();
	GetStringLongValueNotTruncated();
	GetStringTooSmallOutputFails();
	GetStringDecodesUnicodeEscapes();
	GetStringNotFoundAndNonObject();
	GetStringKeyWithEscape();
	KeyLongerThan64Matches();
	FirstDuplicateKeyWins();
	GetNumberScientificDouble();
	GetNumberIntRejectsFraction();
	GetNumberUintRejectsNegative();
	GetNumberOnStringValueFails();
	FindValueBooleanAndObjectReturnRaw();
	SkipValueSkipsWholeObject();
	SkipValueUnterminatedConsumesInput();
	ParserValueTypes();
	ParserBooleanAndNull();

	return VhalCheck::Finish("TestJsonParser");
}
