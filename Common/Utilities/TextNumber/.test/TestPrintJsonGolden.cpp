#include <VHAL.h>
#include <Utilities/Console/Print.h>
#include <Utilities/Serialization/JSON/JSON.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>

// Regression test for Print and JSON: their public outputs, captured before the move onto the TextNumber library, must not change.
// The cases are the same as TestFormat.cpp (GoldenBody.inc), run through Print::NumberToString, Print::FloatToString,
// JSON::Writer::Number, Print::StringToNumber and Print::StringToFloat. Print::StringToFloat is part of the golden list only for
// values its naive method already gets exactly right, so the move can make it more accurate without touching these cases.


namespace {


int failureCount = 0;


void ExpectText(std::string_view actual, std::string_view expected, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: text is \"%.*s\", expected \"%.*s\"\n", line,
			static_cast<int>(actual.size()), actual.data(), static_cast<int>(expected.size()), expected.data());
	}
}


constexpr double kGoldNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kGoldInf = std::numeric_limits<double>::infinity();


template <typename T>
void CheckInteger(T value, int base, const char* expected, int line) {
	char buffer[66] = {};
	size_t length = Print::NumberToString(buffer, sizeof(buffer), value, static_cast<Print::Format>(base));
	ExpectText(std::string_view(buffer, length), expected, line);
}


void CheckFloat(double value, int precision, const char* expected, int line) {
	char buffer[64] = {};
	size_t length = Print::FloatToString(buffer, sizeof(buffer), value, static_cast<uint8_t>(precision));
	ExpectText(std::string_view(buffer, length), expected, line);
}


void CheckJson(double value, int precision, const char* expected, int line) {
	char buffer[64] = {};
	JSON::Writer writer(std::span<char>(buffer, sizeof(buffer)));
	writer.Number(value, precision);
	if (std::string_view(expected).empty()) {
		if (!writer.HasError()) {
			failureCount++;
			std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: a refused value was written\n", line);
		}

		return;
	}

	if (writer.HasError()) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: the writer refused a value it should write\n", line);
		return;
	}

	ExpectText(writer.GetResult(), expected, line);
}


template <typename T>
void CheckParseInteger(const char* text, bool expectOk, T expectedValue, int line) {
	T result{};
	bool ok = Print::StringToNumber<T>(text, result);
	if (ok != expectOk) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: \"%s\" success is %d, expected %d\n", line, text, ok ? 1 : 0,
			expectOk ? 1 : 0);
		return;
	}

	if (expectOk && result != expectedValue) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: \"%s\" parses to %lld\n", line, text, static_cast<long long>(result));
	}
}


void CheckParseFloat(const char* text, bool expectOk, double expectedValue, int line) {
	double result = 0.0;
	bool ok = Print::StringToFloat<double>(text, result);
	if (ok != expectOk) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: \"%s\" success is %d, expected %d\n", line, text, ok ? 1 : 0,
			expectOk ? 1 : 0);
		return;
	}

	if (!expectOk) {
		return;
	}

	// A NaN is compared as a NaN only: Print builds it from 0.0 / 0.0, whose sign bit depends on the platform. A finite value must
	// match exactly, including the sign of zero.
	if (std::isnan(expectedValue)) {
		if (!std::isnan(result)) {
			failureCount++;
			std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: \"%s\" should parse to NaN\n", line, text);
		}

		return;
	}

	if (result != expectedValue || std::signbit(result) != std::signbit(expectedValue)) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: \"%s\" parses to %.17g\n", line, text, result);
	}
}


// A float is read as a float, so it is rounded once. Read through a double, this 19-digit text rounds to the midpoint between 1
// and the next float (1 + 2^-24, which is exact in double) and then to even, which is 1. The host's strtof gives the next float
// up, because the text is above the midpoint. This is the one intended change of StringToFloat, so the check is against strtof
// and not against the old output.
void CheckParseSingle(const char* text, int line) {
	float result = 0.0f;
	bool ok = Print::StringToFloat<float>(text, result);
	float expected = std::strtof(text, nullptr);
	if (!ok || result != expected) {
		failureCount++;
		std::fprintf(stderr, "TestPrintJsonGolden.cpp:%d: float \"%s\" parses to %.9g, strtof gives %.9g\n", line, text,
			static_cast<double>(result), static_cast<double>(expected));
	}
}


#define GOLD_INT(type, value, base, text) CheckInteger<type>(static_cast<type>(value), base, text, __LINE__);
#define GOLD_FLOAT(value, precision, text) CheckFloat((value), precision, text, __LINE__);
#define GOLD_JSON(value, precision, text) CheckJson((value), precision, text, __LINE__);
#define GOLD_PARSE_INT(type, text, ok, value) CheckParseInteger<type>(text, ok, static_cast<type>(value), __LINE__);
#define GOLD_PARSE_FLOAT(text, ok, value) CheckParseFloat(text, ok, (value), __LINE__);


void RunGoldenCases() {
#include "GoldenBody.inc"
}


}


int main() {
	RunGoldenCases();
	CheckParseSingle("1.000000059604644776", __LINE__);
	CheckParseSingle("-1.000000059604644776", __LINE__);
	CheckParseSingle("0.1", __LINE__);

	if (failureCount != 0) {
		std::fprintf(stderr, "TestPrintJsonGolden: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestPrintJsonGolden: all checks passed\n");
	return 0;
}
