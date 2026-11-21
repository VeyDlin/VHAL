#include <Utilities/TextNumber/FloatParser.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// Host tests for FloatParser and ParseFloat: a table and a random set checked against the host's strtod and strtof, inf and
// nan, exponents, many digits, halfway points, Pop at every position, rejected characters changing nothing, and compile-time
// cases.
// Accuracy: ParseFloat is correctly rounded at any length, so its value must equal the host's exactly. The streaming parser
// keeps 19 significant digits: a mantissa of at most 19 digits must give the host's value, and beyond 19 digits the value must
// stay within 2 ulp of it. The largest distance seen is printed, so the bound can be read off the run.


namespace {


int failureCount = 0;
uint64_t maxUlpBeyondNineteenDigits = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: check failed: %s\n", line, expression);
	}
}


#define FLOAT_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)


void ExpectCode(NumberErrorCode actual, NumberErrorCode expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s is code %d, expected %d\n", line, expression,
			static_cast<int>(actual), static_cast<int>(expected));
	}
}


#define FLOAT_CODE(actual, expected) ExpectCode((actual), (expected), #actual, __LINE__)


// Equal as bit patterns, except that any two NaNs are equal
template <typename T>
bool SameValue(T actual, T expected) {
	if (std::isnan(actual) || std::isnan(expected)) {
		return std::isnan(actual) && std::isnan(expected);
	}

	using Bits = std::conditional_t<sizeof(T) == 8, uint64_t, uint32_t>;
	return std::bit_cast<Bits>(actual) == std::bit_cast<Bits>(expected);
}


template <typename T>
void ExpectValue(T actual, T expected, const char* text, int line) {
	if (!SameValue(actual, expected)) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: \"%s\" parses to %.17g, expected %.17g\n", line, text,
			static_cast<double>(actual), static_cast<double>(expected));
	}
}


#define FLOAT_VALUE(type, text, expected) ExpectValue<type>(ParseFloat<type>(text).value, (expected), text, __LINE__)


// The bit patterns of finite values mapped to one ordered integer, so that neighbouring values differ by one
template <typename T>
int64_t OrderedBits(T value) {
	using Bits = std::conditional_t<sizeof(T) == 8, uint64_t, uint32_t>;
	constexpr Bits signBit = static_cast<Bits>(1) << (sizeof(T) * 8 - 1);
	Bits bits = std::bit_cast<Bits>(value);
	int64_t magnitude = static_cast<int64_t>(bits & static_cast<Bits>(~signBit));
	return (bits & signBit) != 0 ? -magnitude : magnitude;
}


template <typename T>
uint64_t UlpDistance(T first, T second) {
	int64_t difference = OrderedBits<T>(first) - OrderedBits<T>(second);
	return static_cast<uint64_t>(difference < 0 ? -difference : difference);
}


// Significant digits of a decimal text: the digits of the mantissa after the leading zeros
size_t SignificantDigits(const std::string& text) {
	size_t count = 0;
	bool started = false;
	for (char character : text) {
		if (character == 'e' || character == 'E') {
			break;
		}

		if (character < '0' || character > '9') {
			continue;
		}

		if (character != '0') {
			started = true;
		}

		if (started) {
			count++;
		}
	}

	return count;
}


template <typename T>
T HostValue(const std::string& text) {
	if constexpr (std::is_same_v<T, float>) {
		return std::strtof(text.c_str(), nullptr);
	} else {
		return std::strtod(text.c_str(), nullptr);
	}
}


// The whole-string result against the host: correctly rounded at any length, so the value must be the host's exactly
template <typename T>
void CheckAgainstHost(const std::string& text, int line) {
	T expected = HostValue<T>(text);
	NumberResult<T> parsed = ParseFloat<T>(text);
	if (!parsed.IsOk()) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s should parse\n", line, text.c_str());
		return;
	}

	if (!SameValue(parsed.value, expected)) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s parses to %.17g, host %.17g\n", line, text.c_str(),
			static_cast<double>(parsed.value), static_cast<double>(expected));
	}
}


// The streaming result against the host: exact for at most 19 significant digits, within 2 ulp beyond that
template <typename T>
void CheckStreamAgainstHost(const std::string& text, int line) {
	T expected = HostValue<T>(text);
	FloatParser<T> parser;
	for (char character : text) {
		parser.Push(character);
	}

	NumberResult<T> streamed = parser.Finish();
	if (!streamed.IsOk()) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s should stream\n", line, text.c_str());
		return;
	}

	if (SameValue(streamed.value, expected)) {
		return;
	}

	if (SignificantDigits(text) <= 19 || std::isinf(expected) || std::isinf(streamed.value)) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s streams to %.17g, host %.17g\n", line, text.c_str(),
			static_cast<double>(streamed.value), static_cast<double>(expected));
		return;
	}

	uint64_t distance = UlpDistance<T>(streamed.value, expected);
	if (distance > maxUlpBeyondNineteenDigits) {
		maxUlpBeyondNineteenDigits = distance;
	}

	if (distance > 2) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: %s is %llu ulp from the host when streamed\n", line, text.c_str(),
			static_cast<unsigned long long>(distance));
	}
}


// A deterministic generator for the random cases
uint64_t randomState = 0xD1B54A32D192ED03ULL;


uint64_t NextRandom() {
	randomState ^= randomState >> 12;
	randomState ^= randomState << 25;
	randomState ^= randomState >> 27;
	return randomState * 2685821657736338717ULL;
}


// Exact decimal text of odd * 2^power. A negative power puts the point before the last -power digits. This is the test's own
// oracle for halfway points, independent of the library's big-integer code.
void MultiplyDecimalDigits(std::vector<uint8_t>& little, uint32_t factor) {
	uint32_t carry = 0;
	for (uint8_t& digit : little) {
		uint32_t product = digit * factor + carry;
		digit = static_cast<uint8_t>(product % 10);
		carry = product / 10;
	}

	while (carry > 0) {
		little.push_back(static_cast<uint8_t>(carry % 10));
		carry /= 10;
	}
}


std::string ExactDecimal(uint64_t odd, int64_t power) {
	std::vector<uint8_t> little;
	while (odd > 0) {
		little.push_back(static_cast<uint8_t>(odd % 10));
		odd /= 10;
	}

	int64_t fractionDigits = 0;
	if (power >= 0) {
		for (int64_t step = 0; step < power; step++) {
			MultiplyDecimalDigits(little, 2);
		}
	} else {
		for (int64_t step = 0; step < -power; step++) {
			MultiplyDecimalDigits(little, 5);
		}

		fractionDigits = -power;
	}

	std::string text;
	for (size_t index = little.size(); index > 0; index--) {
		text.push_back(static_cast<char>('0' + little[index - 1]));
	}

	if (fractionDigits == 0) {
		return text;
	}

	if (static_cast<int64_t>(text.size()) <= fractionDigits) {
		text = std::string(static_cast<size_t>(fractionDigits - static_cast<int64_t>(text.size()) + 1), '0') + text;
	}

	text.insert(text.size() - static_cast<size_t>(fractionDigits), ".");
	return text;
}


// The significand and the power of two of a finite non-negative value: value = significand * 2^power
template <typename T>
void SplitPositive(T value, uint64_t& significand, int64_t& power) {
	using Bits = std::conditional_t<sizeof(T) == 8, uint64_t, uint32_t>;
	constexpr int fractionBits = std::numeric_limits<T>::digits - 1;
	constexpr int exponentBits = static_cast<int>(sizeof(T) * 8) - fractionBits - 1;
	constexpr int bias = std::numeric_limits<T>::max_exponent - 1;
	Bits bits = std::bit_cast<Bits>(value);
	Bits fraction = bits & ((static_cast<Bits>(1) << fractionBits) - 1);
	Bits biased = (bits >> fractionBits) & ((static_cast<Bits>(1) << exponentBits) - 1);
	if (biased == 0) {
		significand = fraction;
		power = std::numeric_limits<T>::min_exponent - std::numeric_limits<T>::digits;
	} else {
		significand = fraction | (static_cast<Bits>(1) << fractionBits);
		power = static_cast<int64_t>(biased) - bias - fractionBits;
	}
}


// A random finite non-negative value, from zero through the subnormal range to the largest value
template <typename T>
T RandomPositiveFinite() {
	using Bits = std::conditional_t<sizeof(T) == 8, uint64_t, uint32_t>;
	constexpr int fractionBits = std::numeric_limits<T>::digits - 1;
	constexpr int exponentBits = static_cast<int>(sizeof(T) * 8) - fractionBits - 1;
	constexpr uint64_t largestBiased = (static_cast<uint64_t>(1) << exponentBits) - 2;
	uint64_t biased = NextRandom() % (largestBiased + 1);
	if (NextRandom() % 8 == 0) {
		biased = 0;
	}

	uint64_t fraction = NextRandom() & ((static_cast<uint64_t>(1) << fractionBits) - 1);
	return std::bit_cast<T>(static_cast<Bits>((biased << fractionBits) | fraction));
}


// The text with more digits after it: a point is added first when the text has none
std::string WithDigitsAfter(const std::string& text, const std::string& digits) {
	if (text.find('.') == std::string::npos) {
		return text + "." + digits;
	}

	return text + digits;
}


// The midpoint between a value and its successor is a tie, so it rounds to the one with an even significand. The text of the
// midpoint is exact, and the same text with one more non-zero digit lies above it and rounds to the successor.
template <typename T>
void CheckHalfway(T value, int line) {
	uint64_t significand = 0;
	int64_t power = 0;
	SplitPositive<T>(value, significand, power);
	std::string halfway = ExactDecimal(2 * significand + 1, power - 1);
	T successor = std::nextafter(value, std::numeric_limits<T>::infinity());
	T tieResult = significand % 2 == 0 ? value : successor;
	ExpectValue<T>(ParseFloat<T>(halfway).value, tieResult, halfway.c_str(), line);
	ExpectValue<T>(ParseFloat<T>(WithDigitsAfter(halfway, "0")).value, tieResult, halfway.c_str(), line);
	ExpectValue<T>(ParseFloat<T>(WithDigitsAfter(halfway, "0000000001")).value, successor, halfway.c_str(), line);
	ExpectValue<T>(ParseFloat<T>(WithDigitsAfter(halfway, "0000000000000000000000000000000000000001")).value, successor,
		halfway.c_str(), line);
}


void TestTableAgainstHost() {
	const char* doubles[] = {"0", "1", "0.1", "0.2", "0.3", "1.5", "2.5", "3.14159", "100", "1e3", "1E3", "1e+3", "1e-3",
		"1.0e-5", "123.456", "9007199254740992", "9007199254740993", "9007199254740995", "0.000001234", "5.", ".5", "0.5e1",
		"1e22", "1e23", "1e-22", "1e-23", "123456789012345", "1234567890123456789", "12345678901234567890",
		"0.1234567890123456789", "2.2250738585072011e-308", "2.2250738585072014e-308", "2.2250738585072009e-308",
		"4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324", "2.5e-324", "1e-323",
		"1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308", "1e308", "1e309", "1e-400", "1e400",
		"0.000000000000000000000000001", "123456789.123456789", "3.14159265358979323846264338327950288",
		"2.718281828459045235360287471352662497757247093699959574966", "0000000000000000000000000000001.5",
		"0.0000000000000000000000000000000000000000000000000000000000000001", "8.5e-5", "6.02214076e23", "1.602176634e-19",
		"4.35e-8", "7e-308", "1.0000000000000002", "0.9999999999999999",
		"1.00000000000000011102230246251565404236316680908203125", "1.00000000000000005551115123125782702118158340454101562"};
	for (const char* text : doubles) {
		CheckAgainstHost<double>(text, __LINE__);
	}

	const char* floats[] = {"0", "1", "0.1", "0.2", "3.4028235e38", "3.4028236e38", "1.17549435e-38", "1.4e-45", "7e-46",
		"16777216", "16777217", "16777219", "1e10", "1e-10", "2.5", "123456.7", "0.3", "1.00000012", "1.00000006"};
	for (const char* text : floats) {
		CheckAgainstHost<float>(text, __LINE__);
	}
}


void TestSpecialValues() {
	FLOAT_VALUE(double, "inf", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "INF", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "iNf", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "-inf", -std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "nan", std::numeric_limits<double>::quiet_NaN());
	FLOAT_VALUE(double, "NaN", std::numeric_limits<double>::quiet_NaN());
	FLOAT_VALUE(double, "-NAN", std::numeric_limits<double>::quiet_NaN());
	FLOAT_VALUE(float, "inf", std::numeric_limits<float>::infinity());
	FLOAT_VALUE(float, "nan", std::numeric_limits<float>::quiet_NaN());

	// "infinity" is read as "inf" and the rest is left, as Print::StringToFloat does
	NumberResult<double> infinity = ParseFloat<double>("infinity");
	FLOAT_EXPECT(infinity.IsOk());
	FLOAT_EXPECT(infinity.endPosition == 3);
	FLOAT_EXPECT(std::isinf(infinity.value));

	// The letters of inf or nan must come whole, and nothing may follow them
	FLOAT_EXPECT(ParseFloat<double>("inf5").endPosition == 3);
	FLOAT_CODE(ParseFloat<double>("in").error.code, NumberErrorCode::InvalidCharacter);
	FLOAT_EXPECT(ParseFloat<double>("1inf").IsOk());
	FLOAT_EXPECT(ParseFloat<double>("1inf").endPosition == 1);
}


void TestOverflowAndUnderflow() {
	FLOAT_VALUE(double, "1e309", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "-1e400", -std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "1e-400", 0.0);
	FLOAT_VALUE(double, "-1e-400", -0.0);
	FLOAT_VALUE(float, "1e39", std::numeric_limits<float>::infinity());
	FLOAT_VALUE(float, "1e-50", 0.0f);

	// Exponent digits far beyond any float keep working
	FLOAT_VALUE(double, "1e99999999999999999999", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "1e-99999999999999999999", 0.0);
	FLOAT_VALUE(double, "0e99999999999999999999", 0.0);
	FLOAT_VALUE(double, "0.0000000000000000000000000001e99999999999999999999", std::numeric_limits<double>::infinity());
	FLOAT_VALUE(double, "100000000000000000000000000000000000000000000000000000000000000e-99999999999999999999", 0.0);

	// Results in the subnormal range are rounded to its grid
	CheckAgainstHost<double>("4.9e-324", __LINE__);
	CheckAgainstHost<double>("2.2250738585072e-308", __LINE__);
	CheckAgainstHost<float>("1.4e-45", __LINE__);
	CheckAgainstHost<float>("1.2e-45", __LINE__);
}


void TestSignsAndPoints() {
	FLOAT_VALUE(double, "-0", -0.0);
	FLOAT_VALUE(double, "0", 0.0);
	FLOAT_VALUE(double, "-2.25", -2.25);
	FLOAT_VALUE(double, "5.", 5.0);
	FLOAT_VALUE(double, ".5", 0.5);
	FLOAT_VALUE(double, "-.5", -0.5);
	FLOAT_CODE(ParseFloat<double>(".").error.code, NumberErrorCode::Empty);
	FLOAT_CODE(ParseFloat<double>("-.").error.code, NumberErrorCode::Empty);
	FLOAT_CODE(ParseFloat<double>("").error.code, NumberErrorCode::Empty);
	FLOAT_CODE(ParseFloat<double>("-").error.code, NumberErrorCode::Empty);
	FLOAT_CODE(ParseFloat<double>("+5").error.code, NumberErrorCode::SignNotAllowed);
	ExpectValue<double>(ParseFloat<double>("+5", FloatOptions{}.WithPlusSign()).value, 5.0, "+5", __LINE__);
	FLOAT_VALUE(double, "-0.5e-1", -0.05);
}


void TestExponents() {
	FLOAT_VALUE(double, "1e5", 100000.0);
	FLOAT_VALUE(double, "1E+5", 100000.0);
	FLOAT_VALUE(double, "1e-5", 1e-5);
	FLOAT_VALUE(double, "2.5E-3", 2.5e-3);
	FLOAT_VALUE(double, "12e0", 12.0);

	// An exponent marker with no digit is not part of the number
	NumberResult<double> bare = ParseFloat<double>("1e");
	FLOAT_EXPECT(bare.IsOk());
	FLOAT_EXPECT(bare.value == 1.0);
	FLOAT_EXPECT(bare.endPosition == 1);
	NumberResult<double> signedBare = ParseFloat<double>("1e+");
	FLOAT_EXPECT(signedBare.IsOk());
	FLOAT_EXPECT(signedBare.endPosition == 1);

	// Streaming reports the missing digits at Finish
	FloatParser<double> streamed;
	streamed.Push('1');
	streamed.Push('e');
	FLOAT_CODE(streamed.Finish().error.code, NumberErrorCode::ExponentWithoutDigits);
	FLOAT_EXPECT(streamed.IsExponentPending());
	FLOAT_CODE(streamed.Push('e').code, NumberErrorCode::InvalidCharacter);

	// One exponent only, and no point after it: the rest is left unread
	FLOAT_EXPECT(ParseFloat<double>("1e5e5").IsOk());
	FLOAT_EXPECT(ParseFloat<double>("1e5e5").endPosition == 3);
	FLOAT_EXPECT(ParseFloat<double>("1e5.5").endPosition == 3);
	FLOAT_CODE(ParseFloat<double>("e5").error.code, NumberErrorCode::InvalidCharacter);
	FLOAT_EXPECT(ParseFloat<double>("1.2.3").endPosition == 3);
}


// An explicit exponent must not be capped while the fraction also has about 1e9 digits: the zeros and the exponent cancel, so
// the value is exactly 1. The text is about a billion characters, streamed one at a time, so nothing is stored.
void TestExponentWithHugeFraction() {
	const uint64_t fractionZeros = 1000000004;
	const std::string exponentDigits = "1000000005";
	FloatParser<double> parser;
	parser.Push('0');
	parser.Push('.');
	for (uint64_t index = 0; index < fractionZeros; index++) {
		parser.Push('0');
	}

	parser.Push('1');
	parser.Push('e');
	for (char digit : exponentDigits) {
		parser.Push(digit);
	}

	NumberResult<double> result = parser.Finish();
	FLOAT_EXPECT(result.IsOk());
	ExpectValue<double>(result.value, 1.0, "0.<1000000004 zeros>1e1000000005", __LINE__);
}


// Many digits: the mantissa keeps 19 significant digits, so the value is correct for the first 19 and close after that
void TestManyDigits() {
	FLOAT_VALUE(double, "00000000000000000000000000000000000000000000000001", 1.0);
	FLOAT_VALUE(double, "0.1000000000000000055511151231257827021181583404541015625", 0.1);
	CheckAgainstHost<double>("123456789012345678901234567890", __LINE__);
	CheckAgainstHost<double>("9007199254740993.00000000000000000000001", __LINE__);
	CheckAgainstHost<double>("0.333333333333333333333333333333333333333333333333333333333333", __LINE__);
	CheckStreamAgainstHost<double>("0.333333333333333333333333333333333333333333333333333333333333", __LINE__);
	CheckStreamAgainstHost<double>("123456789012345678901234567890", __LINE__);
	CheckAgainstHost<double>("1.7976931348623157081452742373170435679807056752584499659891747680315726078002853876058955863276687817154045895351438246423432132688946418276846754670353751698604991057655128207624549009038932894407586850845513394230458323690322294816580855933212334827479782620414472316873817718091929988125040402618412485836", __LINE__);
	CheckAgainstHost<float>("3.402823466385288598117041834845169254400000000000000000000000000000000000001", __LINE__);
}


std::string RandomDecimalText() {
	std::string text;
	if (NextRandom() % 4 == 0) {
		text += '-';
	}

	size_t integerLength = NextRandom() % 13;
	size_t fractionLength = NextRandom() % 13;
	if (integerLength + fractionLength == 0) {
		integerLength = 1;
	}

	for (size_t index = 0; index < integerLength; index++) {
		text += static_cast<char>('0' + NextRandom() % 10);
	}

	if (fractionLength > 0) {
		text += '.';
		for (size_t index = 0; index < fractionLength; index++) {
			text += static_cast<char>('0' + NextRandom() % 10);
		}
	}

	if (NextRandom() % 2 == 0) {
		text += 'e';
		uint64_t sign = NextRandom() % 3;
		if (sign == 0) {
			text += '-';
		} else if (sign == 1) {
			text += '+';
		}

		text += std::to_string(NextRandom() % 340);
	}

	return text;
}


void TestRandomAgainstHost() {
	for (int round = 0; round < 30000; round++) {
		std::string text = RandomDecimalText();
		CheckAgainstHost<double>(text, __LINE__);
		CheckAgainstHost<float>(text, __LINE__);
		CheckStreamAgainstHost<double>(text, __LINE__);
		CheckStreamAgainstHost<float>(text, __LINE__);
	}
}


// A random decimal text with the given number of significant digits (the first one is not zero), the point at a random place
// and an exponent from -400 to 400
std::string RandomLongText(size_t significantDigits) {
	std::string digits(1, static_cast<char>('1' + NextRandom() % 9));
	for (size_t index = 1; index < significantDigits; index++) {
		digits.push_back(static_cast<char>('0' + NextRandom() % 10));
	}

	std::string text;
	if (NextRandom() % 3 == 0) {
		text += '-';
	}

	size_t pointAt = NextRandom() % (significantDigits + 1);
	if (pointAt == 0) {
		text += "0.";
		text += digits;
	} else if (pointAt == significantDigits) {
		text += digits;
	} else {
		text += digits.substr(0, pointAt);
		text += '.';
		text += digits.substr(pointAt);
	}

	int64_t exponent = static_cast<int64_t>(NextRandom() % 801) - 400;
	if (exponent != 0 || NextRandom() % 2 == 0) {
		text += 'e';
		if (exponent < 0) {
			text += '-';
		}

		text += std::to_string(exponent < 0 ? -exponent : exponent);
	}

	return text;
}


// Random texts of 1 to 800 significant digits: the whole-string result is the host's, exactly
void TestLongRandomAgainstHost() {
	for (int round = 0; round < 6000; round++) {
		size_t significantDigits = 1 + NextRandom() % 800;
		std::string text = RandomLongText(significantDigits);
		CheckAgainstHost<double>(text, __LINE__);
		CheckAgainstHost<float>(text, __LINE__);
	}
}


// Halfway points of random values, of both types, with the value itself, zero and the largest value among them
void TestHalfwayPoints() {
	CheckHalfway<double>(0.0, __LINE__);
	CheckHalfway<double>(1.0, __LINE__);
	CheckHalfway<double>(9007199254740992.0, __LINE__);
	CheckHalfway<double>(std::numeric_limits<double>::max(), __LINE__);
	CheckHalfway<double>(std::numeric_limits<double>::denorm_min(), __LINE__);
	CheckHalfway<double>(std::numeric_limits<double>::min(), __LINE__);
	CheckHalfway<float>(0.0f, __LINE__);
	CheckHalfway<float>(1.0f, __LINE__);
	CheckHalfway<float>(16777216.0f, __LINE__);
	CheckHalfway<float>(std::numeric_limits<float>::max(), __LINE__);
	CheckHalfway<float>(std::numeric_limits<float>::denorm_min(), __LINE__);

	for (int round = 0; round < 600; round++) {
		CheckHalfway<double>(RandomPositiveFinite<double>(), __LINE__);
		CheckHalfway<float>(RandomPositiveFinite<float>(), __LINE__);
	}
}


// The examples the library documents: 2^53 + 1 and 1 + 2^-53 are ties, and one more digit breaks the tie
void TestHalfwayExamples() {
	FLOAT_VALUE(double, "9007199254740993", 9007199254740992.0);
	FLOAT_VALUE(double, "9007199254740993.0000000000000000000000001", 9007199254740994.0);
	FLOAT_VALUE(double, "9007199254740995", 9007199254740996.0);
	FLOAT_VALUE(float, "16777217", 16777216.0f);
	FLOAT_VALUE(float, "16777217.000000000000000000000001", 16777218.0f);
	FLOAT_VALUE(double, "1.00000000000000011102230246251565404236316680908203125", 1.0);
	FLOAT_VALUE(double, "1.00000000000000011102230246251565404236316680908203125" "0000000001", 1.0000000000000002);
	// The text stops below 1 + 2^-53 and the extra digits keep it there
	FLOAT_VALUE(double, "1.00000000000000005551115123125782702118158340454101562" "0000000001", 1.0);

	// The smallest subnormal, and the value that rounds to it from above
	FLOAT_VALUE(double, "4.9406564584124654e-324", std::numeric_limits<double>::denorm_min());
	FLOAT_VALUE(float, "1.4e-45", std::numeric_limits<float>::denorm_min());
}


// The halfway points beyond the subnormal range and beyond the largest value are long texts, so the host decides them
void TestHalfwayNeighbours() {
	CheckAgainstHost<double>("2.4703282292062327208828439643e-324", __LINE__);
	CheckAgainstHost<double>("2.4703282292062327208828439644e-324", __LINE__);
	CheckAgainstHost<double>("2.47032822920623272088284399479e-324", __LINE__);
	CheckAgainstHost<double>("1.797693134862315807937289714053e308", __LINE__);
	CheckAgainstHost<double>("1.797693134862315807937289714052e308", __LINE__);
	CheckAgainstHost<double>("1.797693134862315807937289714053000000000000000000000000000001e308", __LINE__);
	CheckAgainstHost<float>("7.0064923216240861e-46", __LINE__);
	CheckAgainstHost<float>("7.00649232162408535461864791644958065640130970938257885878e-46", __LINE__);
}


// Long texts whose value is a near tie, checked against the host with the digits that matter beyond the 19 kept ones
void TestLongNearTies() {
	CheckAgainstHost<double>("0.1000000000000000055511151231257827021181583404541015625000000000000000000000000000000001", __LINE__);
	CheckAgainstHost<double>("0.1000000000000000055511151231257827021181583404541015624999999999999999999999999999999999", __LINE__);
	CheckAgainstHost<double>("2.2250738585072011e-308000000000000000000000000000000000000000000", __LINE__);
	CheckAgainstHost<double>("123456789012345678901234567890123456789012345678901234567890e-50", __LINE__);
	CheckAgainstHost<float>("1.00000005960464477539062500000000000000000000000000000000000000000000000001", __LINE__);
	CheckAgainstHost<float>("3.40282356779733661637539395458142568448000000000000000000000000000000000000001", __LINE__);
}


// Pop exactness. The parser is fed the text up to its first rejection. For every k, the state after k pops must equal a fresh
// parser over the shorter text, and feeding the removed characters again must give the whole result back.
void ExpectSameFloatResult(const NumberResult<double>& actual, const NumberResult<double>& expected, int line) {
	if (actual.error.code != expected.error.code || actual.endPosition != expected.endPosition
		|| actual.error.position != expected.error.position) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: result differs after Pop: code %d/%d end %zu/%zu\n", line,
			static_cast<int>(actual.error.code), static_cast<int>(expected.error.code), actual.endPosition, expected.endPosition);
		return;
	}

	if (actual.IsOk() && !SameValue(actual.value, expected.value)) {
		failureCount++;
		std::fprintf(stderr, "TestFloatParser.cpp:%d: value differs after Pop\n", line);
	}
}


void CheckPopEverywhere(std::string_view text, FloatOptions options, int line) {
	FloatParser<double> whole(options);
	size_t accepted = 0;
	for (; accepted < text.size(); accepted++) {
		if (whole.Push(text[accepted]).IsError()) {
			break;
		}
	}

	NumberResult<double> wholeResult = whole.Finish();
	for (size_t popped = 0; popped <= accepted; popped++) {
		FloatParser<double> parser(options);
		for (size_t index = 0; index < accepted; index++) {
			parser.Push(text[index]);
		}

		for (size_t step = 0; step < popped; step++) {
			parser.Pop();
		}

		FloatParser<double> shorter(options);
		for (size_t index = 0; index < accepted - popped; index++) {
			shorter.Push(text[index]);
		}

		ExpectSameFloatResult(parser.Finish(), shorter.Finish(), line);

		for (size_t index = accepted - popped; index < accepted; index++) {
			parser.Push(text[index]);
		}

		ExpectSameFloatResult(parser.Finish(), wholeResult, line);
	}
}


void TestPopEverywhere() {
	const std::string_view texts[] = {"", "-", "+", ".", "0", "7", "-0", "+5", "1.5", "-2.25", "0.000123", "1e", "1e+",
		"1e-5", "12e34", "-.5e-7", "5.", "inf", "-inf", "nan", "in", "1.2.3", "1e5e5", "0000000000000000000000000001.5",
		"3.14159265358979323846264338327950288", "9007199254740993", "1e400", "123456789012345678901234567890e-30",
		"2.2250738585072011e-308", "12abc", "e5", "1.7976931348623157e308"};
	for (std::string_view text : texts) {
		CheckPopEverywhere(text, FloatOptions{}.WithPlusSign(), __LINE__);
		CheckPopEverywhere(text, FloatOptions{}, __LINE__);
	}

	// Pop of dropped digits: a 25-digit mantissa keeps 19 digits, so popping two digits leaves the 23-digit text
	FloatParser<double> parser;
	for (char character : std::string_view("1234567890123456789012345")) {
		parser.Push(character);
	}

	FLOAT_EXPECT(parser.Finish().IsOk());
	parser.Pop();
	parser.Pop();
	FLOAT_EXPECT(parser.Finish().endPosition == 23);
	FLOAT_EXPECT(SameValue(parser.Finish().value, ParseFloat<double>("12345678901234567890123").value));
	parser.Pop();
	FLOAT_EXPECT(parser.Finish().endPosition == 22);
	FLOAT_EXPECT(SameValue(parser.Finish().value, ParseFloat<double>("1234567890123456789012").value));
}


void TestRejectedCharactersChangeNothing() {
	const char candidates[] = {'a', 'x', '+', '-', ' ', 'i', 'Q', '.', 'e', 'E', '9'};
	const std::string_view prefixes[] = {"", "-", "1", "1.", "1e", "1e5", "inf", "nan", "-0.25"};
	for (std::string_view prefix : prefixes) {
		for (char candidate : candidates) {
			FloatParser<double> parser;
			for (char character : prefix) {
				parser.Push(character);
			}

			NumberResult<double> before = parser.Finish();
			if (parser.Push(candidate).IsError()) {
				ExpectSameFloatResult(parser.Finish(), before, __LINE__);
			}
		}
	}
}


// Compile-time checks: parsing is constant evaluation, including the big-integer path
static_assert(ParseFloat<double>("0.1").value == 0.1);
static_assert(ParseFloat<double>("1e22").value == 1e22);
static_assert(ParseFloat<double>("1e23").value == 1e23);
static_assert(ParseFloat<double>("-2.25").value == -2.25);
static_assert(ParseFloat<double>("9007199254740993").value == 9007199254740992.0);
static_assert(ParseFloat<double>("1.7976931348623157e308").value == 1.7976931348623157e308);
static_assert(ParseFloat<float>("3.4028235e38").value == 3.4028235e38f);
static_assert(ParseFloat<float>("0.1").value == 0.1f);
static_assert(ParseFloat<double>("1e").endPosition == 1);
static_assert(ParseFloat<double>("x").error.code == NumberErrorCode::InvalidCharacter);
static_assert(ParseFloat<double>("+1.5", FloatOptions{}.WithPlusSign()).value == 1.5);
static_assert(ParseFloat<double>("9007199254740993.0000000000000000000000001").value == 9007199254740994.0);
static_assert(ParseFloat<double>("1.00000000000000011102230246251565404236316680908203125").value == 1.0);
static_assert(ParseFloat<double>("1.00000000000000011102230246251565404236316680908203125" "0000000001").value
	== 1.0000000000000002);
static_assert(ParseFloat<double>("0.1000000000000000055511151231257827021181583404541015625000000000000000000001").value
	== 0.1);
static_assert(ParseFloat<double>("4.9406564584124654e-324").value == std::numeric_limits<double>::denorm_min());
static_assert(ParseFloat<float>("16777217.000000000000000000000001").value == 16777218.0f);
static_assert(ParseFloat<float>("16777217").value == 16777216.0f);
static_assert(ParseFloat<double>("   1.5", FloatOptions{}.WithLeadingBlanks()).value == 1.5);

constexpr double kStreamedValue = []() {
	FloatParser<double> parser(FloatOptions{}.WithPlusSign());
	parser.Push('+');
	parser.Push('2');
	parser.Push('.');
	parser.Push('5');
	parser.Push('e');
	parser.Push('3');
	parser.Pop();
	parser.Pop();
	return parser.Finish().value;
}();
static_assert(kStreamedValue == 2.5);


}


int main() {
	TestTableAgainstHost();
	TestSpecialValues();
	TestOverflowAndUnderflow();
	TestSignsAndPoints();
	TestExponents();
	TestExponentWithHugeFraction();
	TestManyDigits();
	TestRandomAgainstHost();
	TestLongRandomAgainstHost();
	TestHalfwayPoints();
	TestHalfwayExamples();
	TestHalfwayNeighbours();
	TestLongNearTies();
	TestPopEverywhere();
	TestRejectedCharactersChangeNothing();

	std::printf("TestFloatParser: largest distance beyond 19 significant digits was %llu ulp\n",
		static_cast<unsigned long long>(maxUlpBeyondNineteenDigits));

	if (failureCount != 0) {
		std::fprintf(stderr, "TestFloatParser: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestFloatParser: all checks passed\n");
	return 0;
}
