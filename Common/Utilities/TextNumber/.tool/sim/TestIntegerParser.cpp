#include <Utilities/TextNumber/IntegerParser.h>
#include <Utilities/TextNumber/IntegerFormat.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// Host tests for IntegerParser and ParseInteger: the range of every integer type, each base and prefix, the sign rules, each
// error with its data, Pop at every position, rejected characters changing nothing, a random cross-check against the host's
// strtoll and strtoull, and compile-time cases.


namespace {


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: check failed: %s\n", line, expression);
	}
}


template <typename T>
void ExpectSame(T actual, T expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		if constexpr (std::is_signed_v<T>) {
			std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s is %lld, expected %lld\n", line, expression,
				static_cast<long long>(actual), static_cast<long long>(expected));
		} else {
			std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s is %llu, expected %llu\n", line, expression,
				static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
		}
	}
}


void ExpectCode(NumberErrorCode actual, NumberErrorCode expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s is code %d, expected %d\n", line, expression,
			static_cast<int>(actual), static_cast<int>(expected));
	}
}


#define INT_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define INT_EQ(actual, expected) ExpectSame((actual), static_cast<decltype(actual)>(expected), #actual, __LINE__)
#define INT_CODE(actual, expected) ExpectCode((actual), (expected), #actual, __LINE__)


// Two results are the same when code, positions and (for a success) the value agree
template <typename T>
void ExpectResultsSame(const NumberResult<T>& actual, const NumberResult<T>& expected, int line) {
	if (actual.error.code != expected.error.code || actual.endPosition != expected.endPosition
		|| actual.error.position != expected.error.position) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: result differs: code %d/%d end %zu/%zu position %zu/%zu\n", line,
			static_cast<int>(actual.error.code), static_cast<int>(expected.error.code), actual.endPosition,
			expected.endPosition, actual.error.position, expected.error.position);
		return;
	}

	if (actual.IsOk() && actual.value != expected.value) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: value differs after Pop\n", line);
	}
}


// Feeds every character of text through the streaming parser and returns its result
template <typename T>
NumberResult<T> StreamText(std::string_view text, IntegerOptions<T> options = {}) {
	IntegerParser<T> parser(options);
	for (char character : text) {
		parser.Push(character);
	}

	return parser.Finish();
}


// Each pair of limits is the text of the type's minimum, maximum, one above and (when it exists) one below the minimum.
template <typename T>
void CheckLimits(std::string_view minimumText, std::string_view maximumText, std::string_view aboveText,
	std::string_view belowText, NumberErrorCode belowCode, int line) {
	NumberResult<T> low = ParseInteger<T>(minimumText);
	NumberResult<T> high = ParseInteger<T>(maximumText);
	NumberResult<T> above = ParseInteger<T>(aboveText);
	NumberResult<T> below = ParseInteger<T>(belowText);

	if (!low.IsOk() || low.value != std::numeric_limits<T>::min()) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: minimum text does not parse to the minimum\n", line);
	}

	if (!high.IsOk() || high.value != std::numeric_limits<T>::max()) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: maximum text does not parse to the maximum\n", line);
	}

	if (above.error.code != NumberErrorCode::OutOfRange || above.error.minimum != std::numeric_limits<T>::min()
		|| above.error.maximum != std::numeric_limits<T>::max()) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: one above the maximum is not out of range with the type's range\n", line);
	}

	ExpectCode(below.error.code, belowCode, "below the minimum", line);
	if (below.error.code == NumberErrorCode::OutOfRange) {
		INT_EQ(below.error.minimum, std::numeric_limits<T>::min());
		INT_EQ(below.error.maximum, std::numeric_limits<T>::max());
	}
}


void TestTypeLimits() {
	CheckLimits<int8_t>("-128", "127", "128", "-129", NumberErrorCode::OutOfRange, __LINE__);
	CheckLimits<uint8_t>("0", "255", "256", "-1", NumberErrorCode::SignNotAllowed, __LINE__);
	CheckLimits<int16_t>("-32768", "32767", "32768", "-32769", NumberErrorCode::OutOfRange, __LINE__);
	CheckLimits<uint16_t>("0", "65535", "65536", "-1", NumberErrorCode::SignNotAllowed, __LINE__);
	CheckLimits<int32_t>("-2147483648", "2147483647", "2147483648", "-2147483649", NumberErrorCode::OutOfRange, __LINE__);
	CheckLimits<uint32_t>("0", "4294967295", "4294967296", "-1", NumberErrorCode::SignNotAllowed, __LINE__);
	CheckLimits<int64_t>("-9223372036854775808", "9223372036854775807", "9223372036854775808", "-9223372036854775809",
		NumberErrorCode::OutOfRange, __LINE__);
	CheckLimits<uint64_t>("0", "18446744073709551615", "18446744073709551616", "-1", NumberErrorCode::SignNotAllowed, __LINE__);

	// Leading zeros and far more digits than the type holds still give the range error, not a wrong value
	NumberResult<uint8_t> longZeros = ParseInteger<uint8_t>("000000000000000000000000000000000000000255");
	INT_EQ(longZeros.value, 255);
	INT_EXPECT(longZeros.IsOk());
	INT_CODE(ParseInteger<uint64_t>("100000000000000000000000").error.code, NumberErrorCode::OutOfRange);
	INT_CODE(ParseInteger<int64_t>("-100000000000000000000000").error.code, NumberErrorCode::OutOfRange);
}


void TestBases() {
	using Signed = IntegerOptions<int32_t>;
	Signed hex = Signed{}.WithHexPrefix();
	Signed binary = Signed{}.WithBinaryPrefix();
	Signed both = Signed{}.WithHexPrefix().WithBinaryPrefix();

	INT_EQ(ParseInteger<int32_t>("0x1F", hex).value, 31);
	INT_EQ(ParseInteger<int32_t>("0X1f", hex).value, 31);
	INT_EQ(ParseInteger<int32_t>("-0x10", hex).value, -16);
	INT_EQ(ParseInteger<int32_t>("0x0", hex).value, 0);
	INT_EQ(ParseInteger<int32_t>("0b1011", binary).value, 11);
	INT_EQ(ParseInteger<int32_t>("0B11", binary).value, 3);
	INT_EQ(ParseInteger<int32_t>("-0b101", binary).value, -5);
	INT_EQ(ParseInteger<int32_t>("0xff", both).value, 255);
	INT_EQ(ParseInteger<int32_t>("0b11", both).value, 3);
	INT_EQ(ParseInteger<uint64_t>("0xFFFFFFFFFFFFFFFF", IntegerOptions<uint64_t>{}.WithHexPrefix()).value,
		std::numeric_limits<uint64_t>::max());
	INT_EQ(ParseInteger<uint32_t>("0xDeadBeef", IntegerOptions<uint32_t>{}.WithHexPrefix()).value, 0xDEADBEEFu);

	// Decimal is the default, with leading zeros allowed
	INT_EQ(ParseInteger<int32_t>("00042").value, 42);
	INT_EQ(ParseInteger<int32_t>("0").value, 0);

	// Without the prefix option, 'x' is not part of the number: the whole-string reader stops before it
	NumberResult<int32_t> noPrefix = ParseInteger<int32_t>("0x1F");
	INT_EXPECT(noPrefix.IsOk());
	INT_EQ(noPrefix.value, 0);
	INT_EQ(noPrefix.endPosition, 1);

	// Hex letters need the 0x prefix
	INT_EQ(ParseInteger<int32_t>("1F").value, 1);
	INT_EQ(ParseInteger<int32_t>("1F").endPosition, 1);

	// A prefix letter is only accepted right after a lone leading zero
	INT_EQ(ParseInteger<int32_t>("10x5", hex).endPosition, 2);
	INT_EQ(ParseInteger<int32_t>("00x5", hex).endPosition, 2);
	INT_EQ(ParseInteger<int32_t>("00x5", hex).value, 0);

	// A binary number takes only 0 and 1
	INT_EQ(ParseInteger<int32_t>("0b102", binary).value, 2);
	INT_EQ(ParseInteger<int32_t>("0b102", binary).endPosition, 4);

	// Prefixes with no digits
	NumberResult<int32_t> prefixOnly = ParseInteger<int32_t>("0x", hex);
	INT_CODE(prefixOnly.error.code, NumberErrorCode::PrefixWithoutDigits);
	INT_EQ(prefixOnly.error.position, 2);
	INT_CODE(ParseInteger<int32_t>("-0b", binary).error.code, NumberErrorCode::PrefixWithoutDigits);

	// "0b2": the 2 is rejected before any binary digit, so the rejection is the reported error
	NumberResult<int32_t> badBinary = ParseInteger<int32_t>("0b2", binary);
	INT_CODE(badBinary.error.code, NumberErrorCode::InvalidCharacter);
	INT_EQ(badBinary.error.position, 2);
	INT_CODE(ParseInteger<int32_t>("0xg", hex).error.code, NumberErrorCode::InvalidCharacter);

	// Streaming: the same rules one character at a time
	INT_CODE(StreamText<int32_t>("0x", hex).error.code, NumberErrorCode::PrefixWithoutDigits);
	INT_EQ(StreamText<int32_t>("0x1f", hex).value, 31);
	// Without the prefix option the 'x' is rejected and changes nothing, so the '1' after it is the next digit: 01
	INT_EQ(StreamText<int32_t>("0x1", Signed{}).value, 1);
	INT_EQ(StreamText<int32_t>("12", Signed{}).value, 12);
}


void TestSigns() {
	using Signed = IntegerOptions<int8_t>;
	using Unsigned = IntegerOptions<uint8_t>;

	// A plus sign is refused unless allowed
	NumberResult<int8_t> plain = ParseInteger<int8_t>("+5");
	INT_CODE(plain.error.code, NumberErrorCode::SignNotAllowed);
	INT_EQ(plain.error.position, 0);
	INT_EQ(ParseInteger<int8_t>("+5", Signed{}.WithPlusSign()).value, 5);
	INT_EQ(ParseInteger<uint8_t>("+5", Unsigned{}.WithPlusSign()).value, 5);

	IntegerParser<int8_t> plusParser;
	INT_CODE(plusParser.Push('+').code, NumberErrorCode::SignNotAllowed);

	// The minus sign is for signed types only, and can be turned off
	INT_EQ(ParseInteger<int8_t>("-5").value, -5);
	INT_CODE(ParseInteger<uint8_t>("-5").error.code, NumberErrorCode::SignNotAllowed);
	INT_CODE(ParseInteger<int8_t>("-5", Signed{}.WithoutMinusSign()).error.code, NumberErrorCode::SignNotAllowed);
	IntegerParser<uint8_t> minusParser;
	INT_CODE(minusParser.Push('-').code, NumberErrorCode::SignNotAllowed);

	// A sign is only the first character
	INT_EQ(ParseInteger<int8_t>("5-").value, 5);
	INT_EQ(ParseInteger<int8_t>("5-").endPosition, 1);
	IntegerParser<int8_t> lateSign;
	lateSign.Push('5');
	INT_CODE(lateSign.Push('-').code, NumberErrorCode::InvalidCharacter);
	NumberResult<int8_t> twoSigns = ParseInteger<int8_t>("--5");
	INT_CODE(twoSigns.error.code, NumberErrorCode::InvalidCharacter);
	INT_EQ(twoSigns.error.position, 1);
	INT_CODE(ParseInteger<int8_t>("+-5", Signed{}.WithPlusSign()).error.code, NumberErrorCode::InvalidCharacter);

	// A sign with no digits is empty, and an empty text is empty
	INT_CODE(ParseInteger<int8_t>("-").error.code, NumberErrorCode::Empty);
	INT_CODE(ParseInteger<int8_t>("+", Signed{}.WithPlusSign()).error.code, NumberErrorCode::Empty);
	INT_EQ(ParseInteger<int8_t>("+", Signed{}.WithPlusSign()).error.position, 1);
	INT_CODE(ParseInteger<int8_t>("").error.code, NumberErrorCode::Empty);
	INT_EQ(ParseInteger<int8_t>("").error.position, 0);

	// Minus zero is zero for a signed type
	INT_EQ(ParseInteger<int8_t>("-0").value, 0);
	INT_CODE(ParseInteger<uint8_t>("-0").error.code, NumberErrorCode::SignNotAllowed);

	// Negative minimum and its magnitude
	INT_EQ(ParseInteger<int64_t>("-9223372036854775808").value, std::numeric_limits<int64_t>::min());
}


void TestRanges() {
	IntegerOptions<uint8_t> tens = IntegerOptions<uint8_t>{}.WithRange(0, 100);
	INT_EQ(ParseInteger<uint8_t>("100", tens).value, 100);
	INT_CODE(ParseInteger<uint8_t>("101", tens).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(ParseInteger<uint8_t>("101", tens).error.maximum, 100);
	INT_EQ(ParseInteger<uint8_t>("101", tens).error.minimum, 0);

	IntegerOptions<uint8_t> middle = IntegerOptions<uint8_t>{}.WithRange(3, 7);
	INT_CODE(ParseInteger<uint8_t>("2", middle).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(ParseInteger<uint8_t>("3", middle).value, 3);
	INT_EQ(ParseInteger<uint8_t>("7", middle).value, 7);
	INT_CODE(ParseInteger<uint8_t>("8", middle).error.code, NumberErrorCode::OutOfRange);
	INT_CODE(ParseInteger<uint8_t>("0", middle).error.code, NumberErrorCode::OutOfRange);

	IntegerOptions<int8_t> around = IntegerOptions<int8_t>{}.WithRange(-5, 5);
	INT_EQ(ParseInteger<int8_t>("-5", around).value, -5);
	INT_EQ(ParseInteger<int8_t>("5", around).value, 5);
	INT_EQ(ParseInteger<int8_t>("-0", around).value, 0);
	INT_CODE(ParseInteger<int8_t>("-6", around).error.code, NumberErrorCode::OutOfRange);
	INT_CODE(ParseInteger<int8_t>("6", around).error.code, NumberErrorCode::OutOfRange);

	IntegerOptions<int8_t> negatives = IntegerOptions<int8_t>{}.WithRange(-50, -10);
	INT_EQ(ParseInteger<int8_t>("-10", negatives).value, -10);
	INT_EQ(ParseInteger<int8_t>("-50", negatives).value, -50);
	INT_CODE(ParseInteger<int8_t>("-9", negatives).error.code, NumberErrorCode::OutOfRange);
	INT_CODE(ParseInteger<int8_t>("-51", negatives).error.code, NumberErrorCode::OutOfRange);
	INT_CODE(ParseInteger<int8_t>("5", negatives).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(ParseInteger<int8_t>("-51", negatives).error.minimum, -50);
	INT_EQ(ParseInteger<int8_t>("-51", negatives).error.maximum, -10);

	// A range that touches the type's own limits
	IntegerOptions<int64_t> wide = IntegerOptions<int64_t>{}.WithRange(std::numeric_limits<int64_t>::min(), -1);
	INT_CODE(ParseInteger<int64_t>("0", wide).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(ParseInteger<int64_t>("-9223372036854775808", wide).value, std::numeric_limits<int64_t>::min());

	// The range is also checked while streaming, at Finish
	IntegerOptions<uint8_t> small = IntegerOptions<uint8_t>{}.WithRange(0, 9);
	INT_CODE(StreamText<uint8_t>("10", small).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(StreamText<uint8_t>("9", small).value, 9);
}


void TestErrors() {
	// Each error code from the streaming parser, with the position of the character that caused it
	IntegerParser<uint8_t> parser;
	NumberError<uint8_t> letter = parser.Push('a');
	INT_CODE(letter.code, NumberErrorCode::InvalidCharacter);
	INT_EQ(letter.position, 0);
	INT_CODE(parser.Push('-').code, NumberErrorCode::SignNotAllowed);
	INT_CODE(parser.Push('2').code, NumberErrorCode::None);
	INT_CODE(parser.Push('5').code, NumberErrorCode::None);
	INT_CODE(parser.Push('6').code, NumberErrorCode::None);
	INT_CODE(parser.Finish().error.code, NumberErrorCode::OutOfRange);
	INT_EQ(parser.Finish().error.maximum, 255);
	INT_EQ(parser.Finish().endPosition, 3);

	// Position in whole-string reads: the rejected character is the error position
	NumberResult<int8_t> nonDigit = ParseInteger<int8_t>("x12");
	INT_CODE(nonDigit.error.code, NumberErrorCode::InvalidCharacter);
	INT_EQ(nonDigit.error.position, 0);
	INT_EQ(nonDigit.endPosition, 0);
	// Blanks are not digits unless the leading-blanks option is on, so the first blank is the reported character
	INT_CODE(ParseInteger<int8_t>("   ").error.code, NumberErrorCode::InvalidCharacter);
	INT_CODE(ParseInteger<int8_t>("   ", IntegerOptions<int8_t>{}.WithLeadingBlanks()).error.code, NumberErrorCode::Empty);
	INT_EQ(ParseInteger<int8_t>("   ", IntegerOptions<int8_t>{}.WithLeadingBlanks()).error.position, 3);
	INT_EQ(ParseInteger<int8_t>("  -3", IntegerOptions<int8_t>{}.WithLeadingBlanks()).value, -3);
	INT_EQ(ParseInteger<int8_t>("  -3", IntegerOptions<int8_t>{}.WithLeadingBlanks()).endPosition, 4);
	INT_CODE(ParseInteger<uint8_t>("  -3", IntegerOptions<uint8_t>{}.WithLeadingBlanks()).error.code,
		NumberErrorCode::SignNotAllowed);
	INT_EQ(ParseInteger<uint8_t>("  -3", IntegerOptions<uint8_t>{}.WithLeadingBlanks()).error.position, 2);

	// Out of range keeps the count of accepted characters and the end
	NumberResult<uint8_t> big = ParseInteger<uint8_t>("300");
	INT_CODE(big.error.code, NumberErrorCode::OutOfRange);
	INT_EQ(big.endPosition, 3);
	INT_EQ(big.error.position, 3);
	INT_EXPECT(!big.IsOk());
}


// Checks that Pop removes exactly the last accepted character: for every k, the parser after k pops equals a fresh parser
// over the shorter text, and feeding the removed characters again gives back the whole result.
template <typename T>
void CheckPopEverywhere(std::string_view text, IntegerOptions<T> options, int line) {
	IntegerParser<T> whole(options);
	size_t accepted = 0;
	for (; accepted < text.size(); accepted++) {
		if (whole.Push(text[accepted]).IsError()) {
			break;
		}
	}

	NumberResult<T> wholeResult = whole.Finish();
	for (size_t popped = 0; popped <= accepted; popped++) {
		IntegerParser<T> parser(options);
		for (size_t index = 0; index < accepted; index++) {
			parser.Push(text[index]);
		}

		for (size_t step = 0; step < popped; step++) {
			parser.Pop();
		}

		IntegerParser<T> shorter(options);
		for (size_t index = 0; index < accepted - popped; index++) {
			shorter.Push(text[index]);
		}

		ExpectResultsSame(parser.Finish(), shorter.Finish(), line);

		for (size_t index = accepted - popped; index < accepted; index++) {
			parser.Push(text[index]);
		}

		ExpectResultsSame(parser.Finish(), wholeResult, line);
	}
}


void TestPopEverywhere() {
	const std::string_view texts[] = {"", "-", "+", "0", "7", "-0", "+5", "256", "2561", "-128", "-129", "0x", "0x1F", "0xff",
		"0b", "0b1011", "0b102", "00x", "-0x7F", "18446744073709551615", "18446744073709551616",
		"123456789012345678901", "1234567890123456789012345", "-9223372036854775808", "9223372036854775807",
		"9223372036854775808", "0000000000000000000000000001", "1x", "12a"};
	for (std::string_view text : texts) {
		CheckPopEverywhere<uint8_t>(text, IntegerOptions<uint8_t>{}.WithHexPrefix().WithBinaryPrefix().WithPlusSign(), __LINE__);
		CheckPopEverywhere<int8_t>(text, IntegerOptions<int8_t>{}.WithHexPrefix().WithBinaryPrefix().WithPlusSign(), __LINE__);
		CheckPopEverywhere<int64_t>(text, IntegerOptions<int64_t>{}.WithHexPrefix().WithBinaryPrefix(), __LINE__);
		CheckPopEverywhere<uint64_t>(text, IntegerOptions<uint64_t>{}.WithHexPrefix().WithPlusSign(), __LINE__);
		CheckPopEverywhere<uint16_t>(text, IntegerOptions<uint16_t>{}.WithRange(10, 500), __LINE__);
	}

	// The digit beyond 64 bits: 2561 typed into uint8 backs up to 256 after one Backspace
	IntegerParser<uint8_t> parser;
	for (char character : std::string_view("2561")) {
		parser.Push(character);
	}

	INT_EQ(parser.Finish().error.code, NumberErrorCode::OutOfRange);
	parser.Pop();
	NumberResult<uint8_t> backedUp = parser.Finish();
	INT_CODE(backedUp.error.code, NumberErrorCode::OutOfRange);
	INT_EQ(backedUp.error.position, 3);
	parser.Pop();
	INT_EQ(parser.Finish().endPosition, 2);
	INT_EQ(parser.Finish().value, 25);
	parser.Pop();
	INT_EQ(parser.Finish().value, 2);
	parser.Pop();
	INT_EQ(parser.Finish().error.code, NumberErrorCode::Empty);

	// Digits beyond 64 bits in a 64-bit type: the count goes up and down with no drift
	IntegerParser<uint64_t> wide;
	for (char character : std::string_view("18446744073709551615")) {
		wide.Push(character);
	}

	INT_EQ(wide.Finish().value, std::numeric_limits<uint64_t>::max());
	wide.Push('0');
	INT_CODE(wide.Finish().error.code, NumberErrorCode::OutOfRange);
	wide.Pop();
	INT_EQ(wide.Finish().value, std::numeric_limits<uint64_t>::max());

	// Pop on an empty parser does nothing
	IntegerParser<int8_t> empty;
	empty.Pop();
	INT_CODE(empty.Finish().error.code, NumberErrorCode::Empty);
	INT_EQ(empty.Finish().endPosition, 0);
}


// Rejected characters change nothing: the result after a rejection equals the result before it
void TestRejectedCharactersChangeNothing() {
	const char candidates[] = {'a', 'x', 'g', '.', ' ', '+', '-', 'Z', '2', '9'};
	const std::string_view prefixes[] = {"", "-", "1", "0x1f", "0b1", "12"};
	for (std::string_view prefix : prefixes) {
		for (char candidate : candidates) {
			IntegerParser<uint8_t> parser(IntegerOptions<uint8_t>{}.WithHexPrefix().WithBinaryPrefix());
			for (char character : prefix) {
				parser.Push(character);
			}

			NumberResult<uint8_t> before = parser.Finish();
			NumberError<uint8_t> rejection = parser.Push(candidate);
			if (rejection.IsError()) {
				ExpectResultsSame(parser.Finish(), before, __LINE__);
			}
		}
	}
}


// A deterministic generator for the cross-check
uint64_t randomState = 0x9E3779B97F4A7C15ULL;


uint64_t NextRandom() {
	randomState ^= randomState >> 12;
	randomState ^= randomState << 25;
	randomState ^= randomState >> 27;
	return randomState * 2685821657736338717ULL;
}


// Random decimal text of 1 to 25 digits, with leading zeros sometimes, and a plus or minus sign when allowed
std::string RandomDecimal(bool allowSign) {
	std::string text;
	uint64_t signChoice = NextRandom() % 4;
	if (allowSign && signChoice == 0) {
		text += '-';
	} else if (allowSign && signChoice == 1) {
		text += '+';
	}

	size_t digitCount = 1 + NextRandom() % 25;
	if (NextRandom() % 5 == 0) {
		text += "000";
	}

	for (size_t index = 0; index < digitCount; index++) {
		text += static_cast<char>('0' + NextRandom() % 10);
	}

	return text;
}


template <typename T>
void CrossCheckSigned(const std::string& text, int line) {
	NumberResult<T> parsed = ParseInteger<T>(text, IntegerOptions<T>{}.WithPlusSign());
	errno = 0;
	long long hostValue = std::strtoll(text.c_str(), nullptr, 10);
	bool inRange = errno == 0 && hostValue >= static_cast<long long>(std::numeric_limits<T>::min())
		&& hostValue <= static_cast<long long>(std::numeric_limits<T>::max());
	if (!inRange) {
		if (parsed.error.code != NumberErrorCode::OutOfRange) {
			failureCount++;
			std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s should be out of range\n", line, text.c_str());
		}
		return;
	}

	if (!parsed.IsOk() || static_cast<long long>(parsed.value) != hostValue) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s parsed wrongly\n", line, text.c_str());
	}
}


template <typename T>
void CrossCheckUnsigned(const std::string& text, int line) {
	NumberResult<T> parsed = ParseInteger<T>(text, IntegerOptions<T>{}.WithPlusSign());
	errno = 0;
	unsigned long long hostValue = std::strtoull(text.c_str(), nullptr, 10);
	bool inRange = errno == 0 && hostValue <= static_cast<unsigned long long>(std::numeric_limits<T>::max());
	if (!inRange) {
		if (parsed.error.code != NumberErrorCode::OutOfRange) {
			failureCount++;
			std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s should be out of range\n", line, text.c_str());
		}
		return;
	}

	if (!parsed.IsOk() || static_cast<unsigned long long>(parsed.value) != hostValue) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: %s parsed wrongly\n", line, text.c_str());
	}
}


void TestAgainstHostConversions() {
	for (int round = 0; round < 20000; round++) {
		std::string signedText = RandomDecimal(true);
		CrossCheckSigned<int8_t>(signedText, __LINE__);
		CrossCheckSigned<int16_t>(signedText, __LINE__);
		CrossCheckSigned<int32_t>(signedText, __LINE__);
		CrossCheckSigned<int64_t>(signedText, __LINE__);

		std::string unsignedText = RandomDecimal(false);
		CrossCheckUnsigned<uint8_t>(unsignedText, __LINE__);
		CrossCheckUnsigned<uint16_t>(unsignedText, __LINE__);
		CrossCheckUnsigned<uint32_t>(unsignedText, __LINE__);
		CrossCheckUnsigned<uint64_t>(unsignedText, __LINE__);
	}
}


// Formatting a value and parsing the text back gives the value. Decimal text carries the sign; hexadecimal and binary text
// carry the two's complement at the type's width, so they are read back as the unsigned type of that width.
template <typename T>
void CheckRoundTrip(T value, int line) {
	using Unsigned = std::make_unsigned_t<T>;

	char decimal[80] = {};
	size_t decimalLength = FormatInteger<T>(std::span<char>(decimal, sizeof(decimal)), value, NumberBase::Decimal);
	IntegerOptions<T> signedOptions = IntegerOptions<T>{}.WithPlusSign();
	NumberResult<T> decimalBack = ParseInteger<T>(std::string_view(decimal, decimalLength), signedOptions);
	if (!decimalBack.IsOk() || decimalBack.value != value) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: decimal round trip failed\n", line);
	}

	char hexadecimal[80] = {};
	size_t hexLength = FormatInteger<T>(std::span<char>(hexadecimal, sizeof(hexadecimal)), value, NumberBase::Hexadecimal);
	std::string hexText = std::string("0x") + std::string(hexadecimal, hexLength);
	NumberResult<Unsigned> hexBack = ParseInteger<Unsigned>(hexText, IntegerOptions<Unsigned>{}.WithHexPrefix());
	if (!hexBack.IsOk() || static_cast<T>(hexBack.value) != value) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: hexadecimal round trip failed\n", line);
	}

	char binary[80] = {};
	size_t binaryLength = FormatInteger<T>(std::span<char>(binary, sizeof(binary)), value, NumberBase::Binary);
	std::string binaryText = std::string("0b") + std::string(binary, binaryLength);
	NumberResult<Unsigned> binaryBack = ParseInteger<Unsigned>(binaryText, IntegerOptions<Unsigned>{}.WithBinaryPrefix());
	if (!binaryBack.IsOk() || static_cast<T>(binaryBack.value) != value) {
		failureCount++;
		std::fprintf(stderr, "TestIntegerParser.cpp:%d: binary round trip failed\n", line);
	}
}


void TestRoundTrips() {
	CheckRoundTrip<uint8_t>(0, __LINE__);
	CheckRoundTrip<uint8_t>(255, __LINE__);
	CheckRoundTrip<int8_t>(-128, __LINE__);
	CheckRoundTrip<int8_t>(127, __LINE__);
	CheckRoundTrip<int16_t>(-12345, __LINE__);
	CheckRoundTrip<uint16_t>(54321, __LINE__);
	CheckRoundTrip<int32_t>(std::numeric_limits<int32_t>::min(), __LINE__);
	CheckRoundTrip<uint32_t>(3735928559U, __LINE__);
	CheckRoundTrip<int64_t>(std::numeric_limits<int64_t>::min(), __LINE__);
	CheckRoundTrip<int64_t>(std::numeric_limits<int64_t>::max(), __LINE__);
	CheckRoundTrip<uint64_t>(std::numeric_limits<uint64_t>::max(), __LINE__);
}


// Digits that overflow the 64-bit magnitude are counted in extraDigits, with a base that is 16 or 2 after a prefix. The
// overflow test has to use that base, so it is checked here: hex against the host's strtoull, binary by hand.
void CrossCheckHexDigits(const std::string& digits, int line) {
	std::string text = "0x" + digits;
	IntegerOptions<uint64_t> options = IntegerOptions<uint64_t>{}.WithHexPrefix();
	errno = 0;
	unsigned long long host = std::strtoull(text.c_str(), nullptr, 16);
	bool hostOverflow = errno == ERANGE;
	NumberResult<uint64_t> streamed = StreamText<uint64_t>(text, options);
	if (hostOverflow) {
		ExpectCode(streamed.error.code, NumberErrorCode::OutOfRange, text.c_str(), line);
	} else {
		ExpectCode(streamed.error.code, NumberErrorCode::None, text.c_str(), line);
		ExpectSame<uint64_t>(streamed.value, static_cast<uint64_t>(host), text.c_str(), line);
	}

	CheckPopEverywhere<uint64_t>(text, options, line);
}


void TestPrefixedOverflow() {
	const IntegerOptions<uint64_t> hexOptions = IntegerOptions<uint64_t>{}.WithHexPrefix();
	const IntegerOptions<uint64_t> binaryOptions = IntegerOptions<uint64_t>{}.WithBinaryPrefix();

	// Sixteen f's is the largest uint64; a seventeenth digit overflows, and one Pop gives the largest value back
	INT_EQ(ParseInteger<uint64_t>("0xffffffffffffffff", hexOptions).value, std::numeric_limits<uint64_t>::max());
	INT_CODE(ParseInteger<uint64_t>("0x1ffffffffffffffff", hexOptions).error.code, NumberErrorCode::OutOfRange);
	IntegerParser<uint64_t> hexParser(hexOptions);
	for (char character : std::string_view("0xffffffffffffffff")) {
		hexParser.Push(character);
	}

	hexParser.Push('f');
	INT_CODE(hexParser.Finish().error.code, NumberErrorCode::OutOfRange);
	hexParser.Pop();
	INT_EQ(hexParser.Finish().value, std::numeric_limits<uint64_t>::max());

	// Leading zeros do not count as digits of the magnitude
	INT_EQ(ParseInteger<uint64_t>("0x000000000000000000ff", hexOptions).value, 255);

	// Binary: sixty-four ones is the largest uint64
	std::string ones(64, '1');
	INT_EQ(ParseInteger<uint64_t>("0b" + ones, binaryOptions).value, std::numeric_limits<uint64_t>::max());
	INT_CODE(ParseInteger<uint64_t>("0b1" + ones, binaryOptions).error.code, NumberErrorCode::OutOfRange);
	INT_EQ(ParseInteger<uint64_t>("0b000" + ones, binaryOptions).value, std::numeric_limits<uint64_t>::max());
	CheckPopEverywhere<uint64_t>("0b1" + ones, binaryOptions, __LINE__);
	CheckPopEverywhere<uint64_t>("0b" + ones, binaryOptions, __LINE__);

	// Hex strings from 15 to 40 digits, with and without leading zeros and in both cases, against strtoull
	const std::string hexDigits = "0123456789abcdefABCDEF";
	for (size_t length = 15; length <= 40; length++) {
		for (int round = 0; round < 4; round++) {
			std::string digits;
			if (round >= 2) {
				digits = std::string(3, '0');
			}

			while (digits.size() < length) {
				digits.push_back(hexDigits[NextRandom() % hexDigits.size()]);
			}

			CrossCheckHexDigits(digits, __LINE__);
		}
	}

	// Sixteen f's after leading zeros, then one more digit that overflows
	CrossCheckHexDigits("000000000000000000000000000000000000000000000ffffffffffffffff", __LINE__);
	CrossCheckHexDigits("ffffffffffffffff0", __LINE__);
}


// Compile-time checks: the parser, the whole-string helper and the option chain are all constant expressions
constexpr IntegerOptions<uint8_t> kByteOptions = IntegerOptions<uint8_t>{}.WithRange(0, 100).WithHexPrefix();

static_assert(ParseInteger<uint8_t>("255").IsOk());
static_assert(ParseInteger<uint8_t>("255").value == 255);
static_assert(ParseInteger<uint8_t>("256").error.code == NumberErrorCode::OutOfRange);
static_assert(ParseInteger<uint8_t>("256").error.maximum == 255);
static_assert(ParseInteger<int8_t>("-128").value == -128);
static_assert(ParseInteger<int8_t>("-129").error.code == NumberErrorCode::OutOfRange);
static_assert(ParseInteger<uint64_t>("18446744073709551615").value == 18446744073709551615ULL);
static_assert(ParseInteger<int64_t>("-9223372036854775808").value == (-9223372036854775807LL - 1));
static_assert(ParseInteger<uint8_t>("0x1F", IntegerOptions<uint8_t>{}.WithHexPrefix()).value == 31);
static_assert(ParseInteger<uint8_t>("0x1F").value == 0);
static_assert(ParseInteger<uint8_t>("50", kByteOptions).value == 50);
static_assert(ParseInteger<uint8_t>("101", kByteOptions).error.code == NumberErrorCode::OutOfRange);
static_assert(ParseInteger<uint8_t>("-1").error.code == NumberErrorCode::SignNotAllowed);
static_assert(ParseInteger<uint32_t>("  77", IntegerOptions<uint32_t>{}.WithLeadingBlanks()).value == 77);

constexpr NumberResult<int16_t> kStreamed = []() {
	IntegerParser<int16_t> parser(IntegerOptions<int16_t>{}.WithPlusSign());
	parser.Push('+');
	parser.Push('4');
	parser.Push('2');
	parser.Push('9');
	parser.Push('9');
	parser.Pop();
	return parser.Finish();
}();
static_assert(kStreamed.IsOk());
static_assert(kStreamed.value == 429);


}


int main() {
	TestTypeLimits();
	TestBases();
	TestSigns();
	TestRanges();
	TestErrors();
	TestPopEverywhere();
	TestRejectedCharactersChangeNothing();
	TestAgainstHostConversions();
	TestRoundTrips();
	TestPrefixedOverflow();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestIntegerParser: %d check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("TestIntegerParser: all checks passed\n");
	return 0;
}
