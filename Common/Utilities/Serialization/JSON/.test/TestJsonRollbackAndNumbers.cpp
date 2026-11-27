#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Serialization/JSON/JSON.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>


// Host tests of the writer's rollback on overflow (a token that does not fit is removed whole, and nothing follows it), and of number
// tokens of any length: long mantissas and exponents, subnormals, underflow and overflow, the JSON grammar, and random decimals and
// printed doubles against strtod.


namespace {


std::string Zeros(size_t count) {
	return std::string(count, '0');
}


std::string Repeat(const std::string& text, size_t count) {
	std::string result;
	for (size_t i = 0; i < count; ++i) {
		result += text;
	}
	return result;
}


bool NearRelative(double actual, double expected, double relativeTolerance) {
	return std::fabs(actual - expected) <= std::fabs(expected) * relativeTolerance;
}


bool ParseDoubleText(const std::string& text, double& value) {
	JSON::Parser parser(text);
	return parser.ParseNumber(value);
}


// The tolerance of a floating-point check: relative to the expected value, and at least 1e-9.
double Tolerance(double expected) {
	return 1e-9 * std::fmax(1.0, std::fabs(expected));
}


} // namespace


void WriterOverflowInsideStringRollsBack() {
	std::array<char, 10> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartObject().KeyValue("k", "abcdef").EndObject();

	VHAL_CHECK(writer.HasError());
	VHAL_CHECK_EQ(writer.GetResult(), R"({"k":)");
	VHAL_CHECK_EQ(writer.GetLength(), writer.GetResult().size());
}


void WriterOverflowInsideIntegerRollsBack() {
	std::array<char, 6> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartArray().Number(static_cast<int64>(123456));

	VHAL_CHECK(writer.HasError());
	VHAL_CHECK_EQ(writer.GetResult(), "[");
}


void WriterOverflowInsideDoubleRollsBack() {
	std::array<char, 4> buffer{};
	JSON::Writer writer{std::span(buffer)};

	writer.StartArray().Number(3.14159);

	VHAL_CHECK(writer.HasError());
	VHAL_CHECK_EQ(writer.GetResult(), "[");
}


void WriterNothingWrittenAfterError() {
	std::array<char, 3> buffer{};
	JSON::Writer writer{std::span(buffer)};

	// The rolled-back number frees room, so only the error flag keeps the later tokens out.
	writer.StartArray().Number(static_cast<int64>(1234)).Comma().Number(static_cast<int64>(1)).EndArray();

	VHAL_CHECK(writer.HasError());
	VHAL_CHECK_EQ(writer.GetResult(), "[");
}


void WriterEveryPrefixEndsAtTokenBoundary() {
	auto write = [](std::span<char> buffer) -> JSON::Writer {
		JSON::Writer writer{buffer};
		writer.StartObject()
			.KeyValue("name", "sensor")
			.Comma()
			.KeyValue("value", static_cast<int64>(-12345))
			.Comma()
			.Key("tags").StartArray().String("a").Comma().Boolean(true).Null().EndArray()
			.EndObject();
		return writer;
	};

	std::array<char, 128> fullBuffer{};
	JSON::Writer full = write(std::span(fullBuffer));
	VHAL_CHECK(!full.HasError());
	const std::string complete(full.GetResult());

	for (size_t size = 0; size < complete.size(); ++size) {
		std::vector<char> small(size);
		JSON::Writer writer = write(std::span(small));
		VHAL_CHECK(writer.HasError());
		VHAL_CHECK_EQ(writer.GetLength(), writer.GetResult().size());
		VHAL_CHECK(complete.compare(0, writer.GetLength(), writer.GetResult().data(), writer.GetLength()) == 0);
		if (writer.GetLength() > 0) {
			// The kept text never ends inside a string: an open quote would leave an odd number of unescaped quotes.
			size_t quotes = 0;
			for (char c : writer.GetResult()) {
				if (c == '"') {
					++quotes;
				}
			}
			VHAL_CHECK(quotes % 2 == 0);
		}
	}
}


void ParserDoubleHundredDigitMantissa() {
	// 100 integer digits followed by a fraction: about 1.2345678901234568e99.
	const std::string json = Repeat("1234567890", 10) + ".5";
	double value = 0.0;
	JSON::Parser parser(json);
	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK(NearRelative(value, 1.2345678901234568e99, 1e-12));
}


void ParserDoubleLongFractionKeepsExponent() {
	// 0.000...0001 with 99 zeros after the point is 1e-100.
	const std::string json = "0." + Zeros(99) + "1";
	double value = 0.0;
	JSON::Parser parser(json);
	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK(NearRelative(value, 1e-100, 1e-12));
}


void ParserDoubleLongZeroFractionWithPositiveExponent() {
	// 0.(399 zeros)1e400 is exactly 1: the exponent is only right if the leading zeros are counted.
	const std::string json = "0." + Zeros(399) + "1e400";
	double value = 0.0;
	JSON::Parser parser(json);
	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK(NearRelative(value, 1.0, 1e-12));
}


void ParserDoubleLongMantissaWithNegativeExponent() {
	// 1 followed by 500 zeros, times 1e-500: exactly 1.
	const std::string json = "1" + Zeros(500) + "e-500";
	double value = 0.0;
	JSON::Parser parser(json);
	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 1.0, Tolerance(1.0));
}


void ParserDoubleUnderflowReadsAsZero() {
	double value = 99.0;
	JSON::Parser p400("1e-400");
	VHAL_CHECK(p400.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 0.0, Tolerance(0.0));

	value = 99.0;
	const std::string longZeros = "0." + Zeros(400) + "1";
	JSON::Parser pLongZeros(longZeros);
	VHAL_CHECK(pLongZeros.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 0.0, Tolerance(0.0));

	value = 99.0;
	JSON::Parser pNegative("-1e-400");
	VHAL_CHECK(pNegative.ParseNumber(value));
	VHAL_CHECK_NEAR(value, 0.0, Tolerance(0.0));
}


void ParserDoubleOverflowStillRejectedForLongToken() {
	double value = 0.0;
	const std::string json = "1" + Zeros(400);
	JSON::Parser parser(json);
	VHAL_CHECK(!parser.ParseNumber(value));
}


void ParserDoubleIntegerTokenOverThirtyOneCharacters() {
	// Valid JSON integer of 72 characters: out of int64 range, but a valid double.
	double value = 0.0;
	const std::string json = "-1" + Zeros(70);
	JSON::Parser parser(json);
	VHAL_CHECK(parser.ParseNumber(value));
	VHAL_CHECK(NearRelative(value, -1e70, 1e-12));
}


void ParserInt64LongIntegerTokenIsOutOfRange() {
	int64 value = 7;
	const std::string json = "1" + Zeros(40);
	JSON::Parser parser(json);
	VHAL_CHECK(!parser.ParseNumber(value));
	VHAL_CHECK_EQ(value, 7);
}


void GetNumberDoubleLongToken() {
	const std::string json = "{\"v\":1" + Zeros(70) + ".5,\"w\":2}";
	double value = 0.0;
	VHAL_CHECK(JSON::GetNumber(json, "v", value));
	VHAL_CHECK(NearRelative(value, 1e70, 1e-12));
}


void FloatSubnormalMatchesStrtod() {
	const std::string text = "1e-320";
	double value = 99.0;
	VHAL_CHECK(ParseDoubleText(text, value));
	VHAL_CHECK(value == std::strtod(text.c_str(), nullptr));
	VHAL_CHECK(value > 0.0);
}


void FloatMinimumSubnormalMatchesStrtod() {
	const std::string text = "4.9406564584124654e-324";
	double value = 0.0;
	VHAL_CHECK(ParseDoubleText(text, value));
	VHAL_CHECK(value == std::strtod(text.c_str(), nullptr));
	VHAL_CHECK(value == std::numeric_limits<double>::denorm_min());
}


void FloatHugeExponentDigitsUnderflowToZero() {
	double value = 99.0;
	VHAL_CHECK(ParseDoubleText("1e-99999999999999999999", value));
	VHAL_CHECK_NEAR(value, 0.0, Tolerance(0.0));
}


void FloatHugeExponentDigitsOverflowRejected() {
	double value = 0.0;
	VHAL_CHECK(!ParseDoubleText("1e99999999999999999999", value));
	VHAL_CHECK(!ParseDoubleText("1.8e308", value));
}


void FloatNegativeZeroKeepsSign() {
	double value = 1.0;
	VHAL_CHECK(ParseDoubleText("-0.0e5", value));
	VHAL_CHECK(value == 0.0);
	VHAL_CHECK(std::signbit(value));
}


void FloatGrammarStillEnforcedForFromChars() {
	// from_chars accepts several of these; the JSON grammar does not.
	const char* const rejected[] = {"+1", "1.", ".5", "01", "1e", "1e+", "inf", "nan", "-", "0x1p3", "1.5.5"};
	for (const char* text : rejected) {
		double value = 7.0;
		const std::string json = text;
		JSON::Parser parser(json);
		// ParseNumber takes the longest valid prefix, so the whole input must be consumed for the token to count.
		VHAL_CHECK(!(parser.ParseNumber(value) && parser.IsAtEnd()));
	}
}


void FloatRandomDecimalsMatchStrtod() {
	std::mt19937_64 rng(0x5eed2026ULL);
	size_t mismatches = 0;
	std::string firstMismatch;

	for (size_t iteration = 0; iteration < 200000; ++iteration) {
		std::string text;
		if (rng() % 2 == 0) {
			text += '-';
		}
		text += static_cast<char>('1' + rng() % 9);
		size_t intDigits = rng() % 40;
		for (size_t i = 0; i < intDigits; ++i) {
			text += static_cast<char>('0' + rng() % 10);
		}
		if (rng() % 2 == 0) {
			text += '.';
			size_t fracDigits = 1 + rng() % 30;
			for (size_t i = 0; i < fracDigits; ++i) {
				text += static_cast<char>('0' + rng() % 10);
			}
		}
		if (rng() % 2 == 0) {
			text += 'e';
			if (rng() % 2 == 0) {
				text += '-';
			} else if (rng() % 3 == 0) {
				text += '+';
			}
			text += std::to_string(rng() % 340);
		}

		const double expected = std::strtod(text.c_str(), nullptr);
		double actual = 0.0;
		const bool ok = ParseDoubleText(text, actual);
		const bool overflow = std::isinf(expected);
		if (overflow ? ok : (!ok || actual != expected)) {
			++mismatches;
			if (firstMismatch.empty()) {
				firstMismatch = text;
			}
		}
	}

	VHAL_CHECK_EQ(mismatches, 0);
	VHAL_CHECK_EQ(firstMismatch, "");
}


void FloatPrintedDoublesRoundTrip() {
	std::mt19937_64 rng(0x0BADF00D2026ULL);
	size_t mismatches = 0;
	std::string firstMismatch;

	for (size_t iteration = 0; iteration < 200000; ++iteration) {
		uint64_t bits = rng();
		double original = 0.0;
		std::memcpy(&original, &bits, sizeof(original));
		if (!std::isfinite(original)) {
			continue;
		}

		char text[64];
		std::snprintf(text, sizeof(text), "%.17g", original);
		double actual = 0.0;
		if (!ParseDoubleText(text, actual) || actual != original) {
			++mismatches;
			if (firstMismatch.empty()) {
				firstMismatch = text;
			}
		}
	}

	VHAL_CHECK_EQ(mismatches, 0);
	VHAL_CHECK_EQ(firstMismatch, "");
}


int main() {
	WriterOverflowInsideStringRollsBack();
	WriterOverflowInsideIntegerRollsBack();
	WriterOverflowInsideDoubleRollsBack();
	WriterNothingWrittenAfterError();
	WriterEveryPrefixEndsAtTokenBoundary();
	ParserDoubleHundredDigitMantissa();
	ParserDoubleLongFractionKeepsExponent();
	ParserDoubleLongZeroFractionWithPositiveExponent();
	ParserDoubleLongMantissaWithNegativeExponent();
	ParserDoubleUnderflowReadsAsZero();
	ParserDoubleOverflowStillRejectedForLongToken();
	ParserDoubleIntegerTokenOverThirtyOneCharacters();
	ParserInt64LongIntegerTokenIsOutOfRange();
	GetNumberDoubleLongToken();
	FloatSubnormalMatchesStrtod();
	FloatMinimumSubnormalMatchesStrtod();
	FloatHugeExponentDigitsUnderflowToZero();
	FloatHugeExponentDigitsOverflowRejected();
	FloatNegativeZeroKeepsSign();
	FloatGrammarStillEnforcedForFromChars();
	FloatRandomDecimalsMatchStrtod();
	FloatPrintedDoublesRoundTrip();

	return VhalCheck::Finish("TestJsonRollbackAndNumbers");
}
