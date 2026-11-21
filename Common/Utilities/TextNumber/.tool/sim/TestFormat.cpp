#include <Utilities/TextNumber/TextNumber.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// Host tests for the number formatting: the golden cases shared with Print and JSON (GoldenBody.inc), round trips, buffers that
// are too small, and compile-time text. The golden values were captured from Print and JSON before they moved onto the library.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define FMT_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)


void ExpectText(std::string_view actual, std::string_view expected, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: text is \"%.*s\", expected \"%.*s\"\n", line, static_cast<int>(actual.size()),
			actual.data(), static_cast<int>(expected.size()), expected.data());
	}
}


constexpr double kGoldNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kGoldInf = std::numeric_limits<double>::infinity();


template <typename T>
void CheckInteger(T value, int base, const char* expected, int line) {
	char buffer[80] = {};
	size_t length = FormatInteger<T>(std::span<char>(buffer, sizeof(buffer)), value, static_cast<NumberBase>(base));
	ExpectText(std::string_view(buffer, length), expected, line);
}


void CheckFloat(double value, int precision, const char* expected, int line) {
	char buffer[80] = {};
	size_t length = FormatFloat<double>(std::span<char>(buffer, sizeof(buffer)), value, static_cast<uint8_t>(precision));
	ExpectText(std::string_view(buffer, length), expected, line);
}


void CheckFixed(double value, int precision, const char* expected, int line) {
	char buffer[80] = {};
	size_t length = FormatFixedPoint(std::span<char>(buffer, sizeof(buffer)), value, precision);
	if (std::string_view(expected).empty()) {
		if (length != 0) {
			failureCount++;
			std::fprintf(stderr, "TestFormat.cpp:%d: a refused value wrote %zu characters\n", line, length);
		}

		return;
	}

	ExpectText(std::string_view(buffer, length), expected, line);
}


template <typename T>
void CheckParseInteger(const char* text, bool expectOk, T expectedValue, int line) {
	NumberResult<T> parsed = ParseInteger<T>(text, IntegerOptions<T>{}.WithPlusSign().WithLeadingBlanks());
	if (parsed.IsOk() != expectOk) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: \"%s\" success is %d, expected %d\n", line, text, parsed.IsOk() ? 1 : 0,
			expectOk ? 1 : 0);
		return;
	}

	if (expectOk && parsed.value != expectedValue) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: \"%s\" parses to %lld\n", line, text, static_cast<long long>(parsed.value));
	}
}


void CheckParseFloat(const char* text, bool expectOk, double expectedValue, int line) {
	NumberResult<double> parsed = ParseFloat<double>(text, FloatOptions{}.WithPlusSign().WithLeadingBlanks());
	if (parsed.IsOk() != expectOk) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: \"%s\" success is %d, expected %d\n", line, text, parsed.IsOk() ? 1 : 0,
			expectOk ? 1 : 0);
		return;
	}

	if (!expectOk) {
		return;
	}

	// A NaN is compared as a NaN only, as in TestPrintJsonGolden.cpp
	if (std::isnan(expectedValue)) {
		if (!std::isnan(parsed.value)) {
			failureCount++;
			std::fprintf(stderr, "TestFormat.cpp:%d: \"%s\" should parse to NaN\n", line, text);
		}

		return;
	}

	if (parsed.value != expectedValue || std::signbit(parsed.value) != std::signbit(expectedValue)) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: \"%s\" parses to %.17g\n", line, text, parsed.value);
	}
}


#define GOLD_INT(type, value, base, text) CheckInteger<type>(static_cast<type>(value), base, text, __LINE__);
#define GOLD_FLOAT(value, precision, text) CheckFloat((value), precision, text, __LINE__);
#define GOLD_JSON(value, precision, text) CheckFixed((value), precision, text, __LINE__);
#define GOLD_PARSE_INT(type, text, ok, value) CheckParseInteger<type>(text, ok, static_cast<type>(value), __LINE__);
#define GOLD_PARSE_FLOAT(text, ok, value) CheckParseFloat(text, ok, (value), __LINE__);


void RunGoldenCases() {
#include "GoldenBody.inc"
}


// A float printed with enough digits parses back to the same double. Forty places keep at least 20 significant digits in
// the fixed form; the scientific form above 2^64 keeps 41. The parser keeps 19 of them, which is still well inside half an ulp.
void CheckFloatRoundTrip(double value, int line) {
	char buffer[80] = {};
	size_t length = FormatFloat<double>(std::span<char>(buffer, sizeof(buffer)), value, 40);
	NumberResult<double> parsed = ParseFloat<double>(std::string_view(buffer, length), FloatOptions{}.WithPlusSign());
	if (!parsed.IsOk() || parsed.value != value) {
		failureCount++;
		std::fprintf(stderr, "TestFormat.cpp:%d: %.*s does not parse back to %.17g\n", line, static_cast<int>(length), buffer, value);
	}
}


void TestFloatRoundTrips() {
	uint64_t state = 0x2545F4914F6CDD1DULL;
	for (int round = 0; round < 20000; round++) {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		uint64_t bits = state * 2685821657736338717ULL;

		// A random fraction in [1, 2) scaled by a power of ten between 1e-20 and 1e300
		double fraction = 1.0 + static_cast<double>(bits >> 12) / 4503599627370496.0;
		int exponent = static_cast<int>(bits % 321) - 20;
		double value = fraction * std::pow(10.0, exponent);
		if ((bits >> 60) % 2 == 1) {
			value = -value;
		}

		CheckFloatRoundTrip(value, __LINE__);
	}

	CheckFloatRoundTrip(0.1, __LINE__);
	CheckFloatRoundTrip(1.0 / 3.0, __LINE__);
	CheckFloatRoundTrip(std::numeric_limits<double>::max(), __LINE__);
	CheckFloatRoundTrip(18446744073709551616.0, __LINE__);
	CheckFloatRoundTrip(1e-20, __LINE__);
}


void TestRefusedBuffers() {
	char small[2] = {'x', 'x'};

	// Too small: nothing is written and the length is zero
	FMT_EXPECT(FormatInteger<uint32_t>(std::span<char>(small, sizeof(small)), 255U) == 0);
	FMT_EXPECT(small[0] == 'x' && small[1] == 'x');
	char tiny[3] = {'x', 'x', 'x'};
	FMT_EXPECT(FormatFloat<double>(std::span<char>(tiny, sizeof(tiny)), 123.45, 2) == 0);
	FMT_EXPECT(tiny[0] == 'x' && tiny[1] == 'x' && tiny[2] == 'x');
	char fixedSmall[3] = {'x', 'x', 'x'};
	FMT_EXPECT(FormatFixedPoint(std::span<char>(fixedSmall, sizeof(fixedSmall)), 12.5, 2) == 0);
	FMT_EXPECT(fixedSmall[0] == 'x' && fixedSmall[2] == 'x');

	// Exactly the size of the text is enough
	char exact[3] = {};
	FMT_EXPECT(FormatInteger<uint32_t>(std::span<char>(exact, sizeof(exact)), 255U) == 3);
	ExpectText(std::string_view(exact, 3), "255", __LINE__);
}


// Compile-time text: the formatting is constant evaluation, floats included
constexpr bool IntegerTextIs(int64_t value, NumberBase base, std::string_view expected) {
	char buffer[80] = {};
	size_t length = FormatInteger<int64_t>(std::span<char>(buffer), value, base);
	return std::string_view(buffer, length) == expected;
}


constexpr bool FloatTextIs(double value, uint8_t precision, std::string_view expected) {
	char buffer[80] = {};
	size_t length = FormatFloat<double>(std::span<char>(buffer), value, precision);
	return std::string_view(buffer, length) == expected;
}


constexpr bool FixedTextIs(double value, int precision, std::string_view expected) {
	char buffer[80] = {};
	size_t length = FormatFixedPoint(std::span<char>(buffer), value, precision);
	return std::string_view(buffer, length) == expected;
}


static_assert(IntegerTextIs(-1, NumberBase::Hexadecimal, "FFFFFFFFFFFFFFFF"));
static_assert(IntegerTextIs(-1, NumberBase::Decimal, "-1"));
static_assert(IntegerTextIs(255, NumberBase::Binary, "0000000000000000000000000000000000000000000000000000000011111111"));
static_assert(IntegerTextIs(0, NumberBase::Octal, "0"));
static_assert(FloatTextIs(2.675, 2, "2.67"));
static_assert(FloatTextIs(-1.5, 0, "-2"));
static_assert(FloatTextIs(1e20, 2, "1.00e+20"));
static_assert(FloatTextIs(kGoldNaN, 2, "NaN"));
static_assert(FloatTextIs(-kGoldInf, 2, "-Inf"));
static_assert(FixedTextIs(-0.0004, 6, "-0.000400"));
static_assert(FixedTextIs(-0.0004, 3, "0.000"));
static_assert(FixedTextIs(2.5, 0, "3"));
static_assert(FixedTextIs(9e18, 0, ""));


}


int main() {
	RunGoldenCases();
	TestFloatRoundTrips();
	TestRefusedBuffers();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestFormat: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestFormat: all checks passed\n");
	return 0;
}
