#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Console/Console.h>

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>


// Host tests of the Console: the marks and the timestamp, the integer and manipulator output, the float and scientific output
// rounded from the exact binary value (ties away from zero), the number and string conversions with their bounds, and the reads.


namespace {


struct Capture {
	Console console;
	std::string out;
	int pendingInput[64] = {};
	size_t inputCount = 0;
	size_t inputPos = 0;

	Capture() {
		console.SetWriteHandler([this](const char* data, size_t size) {
			out.append(data, size);
		});
		console.SetReadHandler([this]() -> int {
			if (inputPos < inputCount) {
				return pendingInput[inputPos++];
			}
			return EOF;
		});
	}

	void Feed(const char* text) {
		inputCount = 0;
		inputPos = 0;
		for (size_t i = 0; text[i] != '\0' && inputCount < 64; i++) {
			pendingInput[inputCount++] = static_cast<unsigned char>(text[i]);
		}
	}
};


} // namespace


void DefaultWarningMarkIsWarning() {
	Console console;
	VHAL_CHECK_EQ(std::string(console.warningMark), std::string("[WARNING]"));
}


void DefaultErrorMarkIsError() {
	Console console;
	VHAL_CHECK_EQ(std::string(console.errorMark), std::string("[ERROR]"));
}


void WarningMethodPrintsWarningMark() {
	VhalSim::SetMs(0);
	Capture capture;
	capture.console.Warning("low battery");
	VHAL_CHECK_EQ(capture.out, std::string("[WARNING] [00:00:00:000] low battery\r\n"));
}


void ErrorMethodPrintsErrorMark() {
	VhalSim::SetMs(0);
	Capture capture;
	capture.console.Error("overtemperature");
	VHAL_CHECK_EQ(capture.out, std::string("[ERROR] [00:00:00:000] overtemperature\r\n"));
}


void StreamWarningAndErrorManipulatorsUseMarks() {
	VhalSim::SetMs(0);
	Capture capture;
	capture.console << Console::warning << "w" << Console::endl;
	capture.console << Console::error << "e" << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("[WARNING] [00:00:00:000] w\r\n[ERROR] [00:00:00:000] e\r\n"));
}


void TimestampHmsmFormat() {
	VhalSim::SetMs(3723004); // 01:02:03.004
	Capture capture;
	capture.console.Info("x");
	VHAL_CHECK_EQ(capture.out, std::string("[INFO] [01:02:03:004] x\r\n"));
}


void TimestampDaysAndMonthsAndYears() {
	Capture capture;

	VhalSim::SetMs((2ULL * 86400ULL + 5ULL * 3600ULL) * 1000ULL + 6ULL);
	capture.console << Console::timestamp << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("[2d 05:00:00:006] \r\n"));

	capture.out.clear();
	VhalSim::SetMs(30ULL * 86400ULL * 1000ULL);
	capture.console << Console::timestamp << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("[1m 00:00:00:000] \r\n"));

	capture.out.clear();
	VhalSim::SetMs(365ULL * 86400ULL * 1000ULL);
	capture.console << Console::timestamp << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("[1y 00:00:00:000] \r\n"));
}


void DecimalZeroNegativeAndExtremes() {
	Capture capture;
	capture.console << 0 << Console::endl;
	capture.console << -5 << Console::endl;
	capture.console << INT_MIN << Console::endl;
	capture.console << UINT_MAX << Console::endl;
	capture.console << static_cast<int8>(-128) << Console::endl;
	capture.console << static_cast<uint8>(255) << Console::endl;
	capture.console << static_cast<int16>(-32768) << Console::endl;
	VHAL_CHECK_EQ(capture.out,
		std::string("0\r\n-5\r\n-2147483648\r\n4294967295\r\n-128\r\n255\r\n-32768\r\n"));
}


void Int64MaxAndUint64Max() {
	Capture capture;
	capture.console << std::numeric_limits<int64>::max() << Console::endl;
	capture.console << std::numeric_limits<uint64>::max() << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("9223372036854775807\r\n18446744073709551615\r\n"));
}


void HexOctBinManipulators() {
	Capture capture;
	capture.console << Console::hex(255) << " " << Console::hex(0) << " " << Console::hex(0x1234ABCDu) << Console::endl;
	capture.console << Console::oct(8) << " " << Console::bin(static_cast<uint8>(5)) << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("FF 0 1234ABCD\r\n10 00000101\r\n"));
}


void HexDumpOfBuffer() {
	Capture capture;
	uint8 buffer[3] = { 0xDE, 0x0A, 0xFF };
	capture.console << Console::hex(buffer, 3);
	VHAL_CHECK_EQ(capture.out, std::string("DE 0A FF "));
}


void FloatFormatting() {
	Capture capture;
	capture.console.Write(3.14159f);
	capture.out += "|";
	capture.console.Write(-2.5, static_cast<uint8>(1));
	capture.out += "|";
	capture.console.Write(0.0, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(1.0 / 3.0, static_cast<uint8>(4));
	capture.out += "|";
	capture.console.Write(1.5, static_cast<uint8>(0));
	// Precision 0 rounds half away from zero: 1.5 prints as 2 (was 1 when digits were truncated)
	VHAL_CHECK_EQ(capture.out, std::string("3.14|-2.5|0.00|0.3333|2"));
}


void FloatRoundsToRequestedPrecision() {
	Capture capture;
	capture.console.Write(0.999, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(0.994, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(9.999, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(2.5, static_cast<uint8>(0));
	capture.out += "|";
	capture.console.Write(-2.5, static_cast<uint8>(0));
	capture.out += "|";
	capture.console.Write(0.125, static_cast<uint8>(2));
	capture.out += "|";
	// -0.995 is stored just below the tie, so the exact value rounds to 0.99
	capture.console.Write(-0.995, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(3.14159, static_cast<uint8>(4));
	VHAL_CHECK_EQ(capture.out, std::string("1.00|0.99|10.00|3|-3|0.13|-0.99|3.1416"));
}


void LargeFiniteFloatsUseScientificNotation() {
	Capture capture;
	capture.console.Write(1e20, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(1.7976931348623157e308, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(-1e300, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(1e20, static_cast<uint8>(0));
	capture.out += "|";
	capture.console.Write(9.9999e20, static_cast<uint8>(2));
	VHAL_CHECK_EQ(capture.out, std::string("1.00e+20|1.80e+308|-1.00e+300|1e+20|1.00e+21"));
}


void FloatAtTwoToThe64BoundaryPrintsInFixedNotation() {
	Capture capture;
	capture.console.Write(18446744073709549568.0, static_cast<uint8>(0));
	capture.out += "|";
	capture.console.Write(18446744073709551616.0, static_cast<uint8>(1));
	VHAL_CHECK_EQ(capture.out, std::string("18446744073709549568|1.8e+19"));
}


void NegativeSignedValuesInHexOctAndBinUseTypeWidth() {
	Capture capture;
	capture.console << Console::hex(static_cast<int8>(-1)) << " ";
	capture.console << Console::hex(static_cast<int16>(-1)) << " ";
	capture.console << Console::hex(static_cast<int32>(-1)) << " ";
	capture.console << Console::hex(static_cast<int64>(-1)) << " ";
	capture.console << Console::oct(static_cast<int8>(-1)) << " ";
	capture.console << Console::bin(static_cast<int8>(-1)) << " ";
	capture.console << Console::hex(static_cast<int8>(-2)) << Console::endl;
	VHAL_CHECK_EQ(capture.out,
		std::string("FF FFFF FFFFFFFF FFFFFFFFFFFFFFFF 377 11111111 FE\r\n"));
}


void BinOfIntUsesThirtyTwoDigits() {
	Capture capture;
	capture.console << Console::bin(255) << Console::endl;
	VHAL_CHECK_EQ(capture.out, std::string("00000000000000000000000011111111\r\n"));
}


void NumberToStringNegativeHexDecimalUnchanged() {
	char buffer[32] = {};
	size_t len = Print::NumberToString(buffer, sizeof(buffer), static_cast<int32>(-42), Print::Format::Dec);
	VHAL_CHECK_EQ(std::string(buffer, len), std::string("-42"));
}


void FloatNegativeZeroAndSpecials() {
	Capture capture;
	capture.console.Write(-0.0, static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(std::nan(""), static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(std::numeric_limits<double>::infinity(), static_cast<uint8>(2));
	capture.out += "|";
	capture.console.Write(-std::numeric_limits<double>::infinity(), static_cast<uint8>(2));
	VHAL_CHECK_EQ(capture.out, std::string("0.00|NaN|Inf|-Inf"));
}


void FiniteFloatNearDoubleMaxIsNotInf() {
	// The Inf branch tests number > 1e308, which also matches finite values up to DBL_MAX.
	Capture capture;
	capture.console.Write(1.5e308, static_cast<uint8>(0));
	VHAL_CHECK(capture.out != "Inf");
}


void NumberToStringBounds() {
	char buffer[8] = {};
	size_t len = Print::NumberToString(buffer, 3, 12345, Print::Format::Dec);
	VHAL_CHECK_EQ(len, static_cast<size_t>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("12"));

	char tiny[2] = { 'x', 'x' };
	size_t tinyLen = Print::NumberToString(tiny, 1, 12345, Print::Format::Dec);
	VHAL_CHECK_EQ(tinyLen, static_cast<size_t>(0));
	VHAL_CHECK_EQ(tiny[0], '\0');
}


void StringToNumberRangeChecks() {
	int8 i8 = 0;
	VHAL_CHECK(Print::StringToNumber("127", i8));
	VHAL_CHECK_EQ(i8, static_cast<int8>(127));
	VHAL_CHECK(Print::StringToNumber("-128", i8));
	VHAL_CHECK_EQ(i8, static_cast<int8>(-128));
	VHAL_CHECK(!Print::StringToNumber("128", i8));
	VHAL_CHECK(!Print::StringToNumber("-129", i8));

	uint8 u8 = 0;
	VHAL_CHECK(Print::StringToNumber("255", u8));
	VHAL_CHECK_EQ(u8, static_cast<uint8>(255));
	VHAL_CHECK(!Print::StringToNumber("256", u8));
	VHAL_CHECK(!Print::StringToNumber("-1", u8));

	uint64 u64 = 0;
	VHAL_CHECK(Print::StringToNumber("18446744073709551615", u64));
	VHAL_CHECK_EQ(u64, std::numeric_limits<uint64>::max());
	VHAL_CHECK(!Print::StringToNumber("18446744073709551616", u64));
}


void StringToNumberInt64MinimumIsAccepted() {
	int64 value = 0;
	VHAL_CHECK(Print::StringToNumber("-9223372036854775808", value));
	VHAL_CHECK(value == std::numeric_limits<int64>::min());
}


void NumberToStringInt64MinimumHasNoOverflow() {
	char buffer[32] = {};
	size_t len = Print::NumberToString(buffer, sizeof(buffer), std::numeric_limits<int64>::min(), Print::Format::Dec);
	VHAL_CHECK_EQ(std::string(buffer, len), std::string("-9223372036854775808"));
}


void ReadParsesInteger() {
	Capture capture;
	capture.Feed("42");
	VHAL_CHECK_EQ(capture.console.Read<int>(), 42);
}


void ReadWithZeroBufferSizeStaysInBounds() {
	Capture capture;
	capture.Feed("abcde");
	char* buffer = new char[1];
	buffer[0] = 'z';
	size_t count = capture.console.Read(buffer, 0);
	delete[] buffer;
	VHAL_CHECK_EQ(count, static_cast<size_t>(0));
}


void ReadLineStopsAtNewline() {
	Capture capture;
	capture.Feed("ab\ncd");
	char buffer[16] = {};
	size_t count = capture.console.ReadLine(buffer, sizeof(buffer));
	VHAL_CHECK_EQ(count, static_cast<size_t>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("ab"));
}


// Rounding uses the exact binary value of the double, not a decimal guess

void FloatRoundsExactBinaryValueBelowTie() {
	// 0.15 is stored as 0.1499999999999999944...: below the tie, so it rounds down
	char buffer[64] = {};
	Print::FloatToString(buffer, sizeof(buffer), 0.15, static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("0.1"));

	// 2.675 is stored as 2.67499999999999982236...
	Print::FloatToString(buffer, sizeof(buffer), 2.675, static_cast<uint8>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("2.67"));

	// 1.005 is stored as 1.00499999999999989341...
	Print::FloatToString(buffer, sizeof(buffer), 1.005, static_cast<uint8>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.00"));
}


void FloatExactBinaryTiesRoundAwayFromZero() {
	// These ties are exact in binary. printf would round them half to even (0.12, 1.062),
	// this formatter rounds half away from zero.
	char buffer[64] = {};
	Print::FloatToString(buffer, sizeof(buffer), 0.125, static_cast<uint8>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("0.13"));

	Print::FloatToString(buffer, sizeof(buffer), 0.375, static_cast<uint8>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("0.38"));

	Print::FloatToString(buffer, sizeof(buffer), 0.25, static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("0.3"));

	Print::FloatToString(buffer, sizeof(buffer), -0.25, static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("-0.3"));

	Print::FloatToString(buffer, sizeof(buffer), 1.0625, static_cast<uint8>(3));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.063"));
}


void FloatDecimalInputsMatchCorrectlyRoundedPrintf() {
	// Sweep n/1000 for n in [0, 20000) at precisions 1 and 2, the inputs most likely to sit on a
	// decimal tie. printf("%.*f") rounds the exact binary value correctly; exact binary ties are
	// skipped here because printf rounds them half to even (covered by the test above).
	const uint8 precisions[] = { 1, 2 };
	int mismatches = 0;
	std::string firstMismatch;

	for (uint8 precision : precisions) {
		for (int n = 0; n < 20000; n++) {
			double value = static_cast<double>(n) / 1000.0;

			char exact[128] = {};
			std::snprintf(exact, sizeof(exact), "%.80f", value);
			std::string text(exact);
			size_t point = text.find('.');
			size_t tail = point + 1 + precision;
			bool isExactTie = text[tail] == '5' && text.find_first_not_of('0', tail + 1) == std::string::npos;
			if (isExactTie) {
				continue;
			}

			char expected[128] = {};
			std::snprintf(expected, sizeof(expected), "%.*f", static_cast<int>(precision), value);

			char actual[128] = {};
			Print::FloatToString(actual, sizeof(actual), value, precision);
			if (std::string(actual) != std::string(expected)) {
				mismatches++;
				if (firstMismatch.empty()) {
					firstMismatch = std::string("value ") + exact + " precision " + std::to_string(precision) +
						" got " + actual + " expected " + expected;
				}
			}
		}
	}

	VHAL_CHECK_EQ(mismatches, 0);
	VHAL_CHECK_EQ(firstMismatch, std::string());
}


// Scientific notation (values of 2^64 and above) rounds the exact value as well

void ScientificMatchesCorrectlyRoundedPrintfSweep() {
	// printf("%.*e") is the reference. Ties that are exact in binary are skipped here and
	// asserted separately as half away from zero.
	const uint8 precisions[] = { 0, 1, 2, 5, 10, 17, 25, 40 };
	std::vector<double> values;
	for (int e = 64; e <= 1023; e++) {
		values.push_back(std::ldexp(1.0, e));
	}
	for (int e = 20; e <= 308; e++) {
		char text[32] = {};
		std::snprintf(text, sizeof(text), "1e%d", e);
		values.push_back(std::strtod(text, nullptr));
	}
	values.push_back(std::numeric_limits<double>::max());

	uint64 state = 0x9E3779B97F4A7C15ull;
	for (int i = 0; i < 2000; i++) {
		state = state * 6364136223846793005ull + 1442695040888963407ull;
		double fraction = static_cast<double>(state >> 11) / 9007199254740992.0;
		int exponent = 65 + static_cast<int>((state >> 40) % 959);
		values.push_back(std::ldexp(0.5 + fraction / 2.0, exponent));
	}

	int mismatches = 0;
	std::string firstMismatch;
	for (double value : values) {
		for (uint8 precision : precisions) {
			char exact[512] = {};
			std::snprintf(exact, sizeof(exact), "%.340e", value);
			std::string text(exact);
			size_t ePos = text.find('e');
			size_t nextDigit = precision + 2;
			bool isExactTie = text[nextDigit] == '5' &&
				text.find_first_not_of('0', nextDigit + 1) == ePos;
			if (isExactTie) {
				continue;
			}

			char printed[64] = {};
			std::snprintf(printed, sizeof(printed), "%.*e", static_cast<int>(precision), value);
			std::string expected(printed);
			size_t expPos = expected.find('e');
			expected = expected.substr(0, expPos) + "e+" + std::to_string(std::atoi(expected.c_str() + expPos + 1));

			char actual[128] = {};
			Print::FloatToString(actual, sizeof(actual), value, precision);
			if (std::string(actual) != expected) {
				mismatches++;
				if (firstMismatch.empty()) {
					firstMismatch = std::string("value ") + exact + " precision " + std::to_string(precision) +
						" got " + actual + " expected " + expected;
				}
			}
		}
	}

	VHAL_CHECK_EQ(mismatches, 0);
	VHAL_CHECK_EQ(firstMismatch, std::string());
}


void ScientificExactTiesRoundAwayFromZero() {
	// All three are exact in binary, so printf would round the last digit to even (1.12e+20).
	char buffer[64] = {};
	Print::FloatToString(buffer, sizeof(buffer), 2.5e20, static_cast<uint8>(0));
	VHAL_CHECK_EQ(std::string(buffer), std::string("3e+20"));

	Print::FloatToString(buffer, sizeof(buffer), 1.25e20, static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.3e+20"));

	Print::FloatToString(buffer, sizeof(buffer), 1.125e20, static_cast<uint8>(2));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.13e+20"));

	Print::FloatToString(buffer, sizeof(buffer), -1.25e20, static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("-1.3e+20"));
}


void ScientificDblMaxAtSeveralPrecisions() {
	char buffer[64] = {};
	Print::FloatToString(buffer, sizeof(buffer), std::numeric_limits<double>::max(), static_cast<uint8>(0));
	VHAL_CHECK_EQ(std::string(buffer), std::string("2e+308"));

	Print::FloatToString(buffer, sizeof(buffer), std::numeric_limits<double>::max(), static_cast<uint8>(1));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.8e+308"));

	Print::FloatToString(buffer, sizeof(buffer), std::numeric_limits<double>::max(), static_cast<uint8>(5));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.79769e+308"));

	// DBL_MAX = 1.79769313486231570814...e308, so the 18th digit rounds up
	Print::FloatToString(buffer, sizeof(buffer), std::numeric_limits<double>::max(), static_cast<uint8>(17));
	VHAL_CHECK_EQ(std::string(buffer), std::string("1.79769313486231571e+308"));
}


int main() {
	DefaultWarningMarkIsWarning();
	DefaultErrorMarkIsError();
	WarningMethodPrintsWarningMark();
	ErrorMethodPrintsErrorMark();
	StreamWarningAndErrorManipulatorsUseMarks();
	TimestampHmsmFormat();
	TimestampDaysAndMonthsAndYears();
	DecimalZeroNegativeAndExtremes();
	Int64MaxAndUint64Max();
	HexOctBinManipulators();
	HexDumpOfBuffer();
	FloatFormatting();
	FloatRoundsToRequestedPrecision();
	LargeFiniteFloatsUseScientificNotation();
	FloatAtTwoToThe64BoundaryPrintsInFixedNotation();
	NegativeSignedValuesInHexOctAndBinUseTypeWidth();
	BinOfIntUsesThirtyTwoDigits();
	NumberToStringNegativeHexDecimalUnchanged();
	FloatNegativeZeroAndSpecials();
	FiniteFloatNearDoubleMaxIsNotInf();
	NumberToStringBounds();
	StringToNumberRangeChecks();
	StringToNumberInt64MinimumIsAccepted();
	NumberToStringInt64MinimumHasNoOverflow();
	ReadParsesInteger();
	ReadWithZeroBufferSizeStaysInBounds();
	ReadLineStopsAtNewline();
	FloatRoundsExactBinaryValueBelowTie();
	FloatExactBinaryTiesRoundAwayFromZero();
	FloatDecimalInputsMatchCorrectlyRoundedPrintf();
	ScientificMatchesCorrectlyRoundedPrintfSweep();
	ScientificExactTiesRoundAwayFromZero();
	ScientificDblMaxAtSeveralPrecisions();

	return VhalCheck::Finish("TestConsole");
}
