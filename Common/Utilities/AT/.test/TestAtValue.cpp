#include <VHAL.h>
#include <Utilities/AT/AtValue.h>
#include <Utilities/Console/Print.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

// Host tests of the AT value grammar: the readers that take a field one character at a time, and the writers that print a value. The
// expected results are the grammar's: the TextNumber rules for integers (signed decimal, no plus sign, the type's range), 0 or 1 for a bool,
// and quotes with \" and \\ escapes for a string. Each expectation is written here by hand.


namespace {


int failureCount = 0;


void ExpectEqual(long long actual, long long expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtValue.cpp:%d: %s is %lld, expected %lld\n", line, expression, actual, expected);
	}
}


void ExpectText(const std::string& actual, const std::string& expected, const char* expression, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestAtValue.cpp:%d: %s is \"%s\", expected \"%s\"\n", line, expression, actual.c_str(), expected.c_str());
	}
}


#define AV_EQ(actual, expected) ExpectEqual(static_cast<long long>(actual), static_cast<long long>(expected), #actual, __LINE__)
#define AV_TEXT(actual, expected) ExpectText(actual, expected, #actual, __LINE__)


// A Print that keeps its text.
class CaptureOut : public Print {
public:
	std::string text;


	void WriteRaw(const char* data, size_t size) override {
		text.append(data, size);
	}
};


// Feeds the whole field to a reader. accepted is false when Push refused any character, and the status is Finish's.
template<typename T>
ResultStatus ReadField(std::string_view field, T& destination, bool& accepted) {
	AtValueReader<T> reader;
	accepted = true;

	for (char character : field) {
		if (!reader.Push(character)) {
			accepted = false;
		}
	}

	return reader.Finish(destination);
}


// The text a value prints as.
template<typename T>
std::string PrintedText(const T& value) {
	CaptureOut out;
	AtWriteValue(out, value);
	return out.text;
}


int TestIntegers() {
	int before = failureCount;
	bool accepted = false;
	int8 small = 0;
	uint8 byteValue = 0;
	int64 wide = 0;
	uint32 wideUnsigned = 0;
	int16 untouched = 77;

	AV_EQ(ReadField<int8>("-128", small, accepted), ResultStatus::ok);
	AV_EQ(small, -128);
	AV_EQ(accepted, true);

	AV_EQ(ReadField<int8>("127", small, accepted), ResultStatus::ok);
	AV_EQ(small, 127);

	AV_EQ(ReadField<int8>("128", small, accepted), ResultStatus::outOfRange);
	AV_EQ(ReadField<int8>("-129", small, accepted), ResultStatus::outOfRange);
	AV_EQ(ReadField<int8>("", small, accepted), ResultStatus::invalidData);
	AV_EQ(ReadField<int8>("1a", small, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);
	AV_EQ(ReadField<int8>("+5", small, accepted), ResultStatus::invalidData);

	AV_EQ(ReadField<uint8>("255", byteValue, accepted), ResultStatus::ok);
	AV_EQ(byteValue, 255);
	AV_EQ(ReadField<uint8>("256", byteValue, accepted), ResultStatus::outOfRange);
	AV_EQ(ReadField<uint8>("-1", byteValue, accepted), ResultStatus::invalidData);

	AV_EQ(ReadField<int64>("-9223372036854775808", wide, accepted), ResultStatus::ok);
	AV_EQ(wide == INT64_MIN, true);
	AV_EQ(ReadField<int64>("9223372036854775808", wide, accepted), ResultStatus::outOfRange);

	AV_EQ(ReadField<uint32>("4294967295", wideUnsigned, accepted), ResultStatus::ok);
	AV_EQ(wideUnsigned, 4294967295u);
	AV_EQ(ReadField<uint32>("4294967296", wideUnsigned, accepted), ResultStatus::outOfRange);

	int16 refusedTarget = untouched;
	AV_EQ(ReadField<int16>("1x2", refusedTarget, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);
	AV_EQ(refusedTarget, 77);

	return failureCount - before;
}


int TestBool() {
	int before = failureCount;
	bool accepted = false;
	bool flag = false;

	AV_EQ(ReadField<bool>("0", flag, accepted), ResultStatus::ok);
	AV_EQ(flag, false);
	AV_EQ(ReadField<bool>("1", flag, accepted), ResultStatus::ok);
	AV_EQ(flag, true);
	AV_EQ(ReadField<bool>("2", flag, accepted), ResultStatus::invalidData);
	AV_EQ(ReadField<bool>("", flag, accepted), ResultStatus::invalidData);
	AV_EQ(ReadField<bool>("10", flag, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);

	return failureCount - before;
}


int TestStrings() {
	int before = failureCount;
	bool accepted = false;
	String<8> text{};

	AV_EQ(ReadField<String<8>>("\"abc\"", text, accepted), ResultStatus::ok);
	AV_EQ(text.length, 3);
	AV_EQ(text.View() == "abc", true);

	AV_EQ(ReadField<String<8>>("\"\"", text, accepted), ResultStatus::ok);
	AV_EQ(text.length, 0);

	AV_EQ(ReadField<String<8>>("\"a\\\"b\"", text, accepted), ResultStatus::ok);
	AV_EQ(text.View() == "a\"b", true);

	AV_EQ(ReadField<String<8>>("\"a\\\\b\"", text, accepted), ResultStatus::ok);
	AV_EQ(text.View() == "a\\b", true);

	AV_EQ(ReadField<String<8>>("\"a\\x\"", text, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);
	AV_EQ(ReadField<String<8>>("\"abc", text, accepted), ResultStatus::invalidData);
	AV_EQ(ReadField<String<8>>("\"ab\"x", text, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);
	AV_EQ(ReadField<String<8>>("abc", text, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);

	String<3> limited{};
	AV_EQ(ReadField<String<3>>("\"abc\"", limited, accepted), ResultStatus::ok);
	AV_EQ(limited.View() == "abc", true);
	AV_EQ(ReadField<String<3>>("\"abcd\"", limited, accepted), ResultStatus::invalidData);
	AV_EQ(accepted, false);

	return failureCount - before;
}


int TestWriters() {
	int before = failureCount;

	AV_TEXT(PrintedText<int8>(-5), "-5");
	AV_TEXT(PrintedText<uint8>(0), "0");
	AV_TEXT(PrintedText<uint32>(4294967295u), "4294967295");
	AV_TEXT(PrintedText<int64>(INT64_MIN), "-9223372036854775808");
	AV_TEXT(PrintedText<bool>(true), "1");
	AV_TEXT(PrintedText<bool>(false), "0");

	String<16> text{};
	text.characters[0] = 'a';
	text.characters[1] = '"';
	text.characters[2] = 'b';
	text.characters[3] = '\\';
	text.characters[4] = 'c';
	text.length = 5;
	AV_TEXT(PrintedText<String<16>>(text), "\"a\\\"b\\\\c\"");

	String<4> empty{};
	AV_TEXT(PrintedText<String<4>>(empty), "\"\"");

	return failureCount - before;
}


int TestRoundTrip() {
	int before = failureCount;
	bool accepted = false;

	String<16> original{};
	original.characters[0] = 'x';
	original.characters[1] = '"';
	original.characters[2] = '\\';
	original.characters[3] = ',';
	original.length = 4;

	String<16> back{};
	AV_EQ(ReadField<String<16>>(PrintedText<String<16>>(original), back, accepted), ResultStatus::ok);
	AV_EQ(back.View() == original.View(), true);

	int16 negative = -1234;
	int16 read = 0;
	AV_EQ(ReadField<int16>(PrintedText<int16>(negative), read, accepted), ResultStatus::ok);
	AV_EQ(read, -1234);

	return failureCount - before;
}


static_assert(AtValueChars<int8> == 4);
static_assert(AtValueChars<int16> == 6);
static_assert(AtValueChars<int32> == 11);
static_assert(AtValueChars<int64> == 20);
static_assert(AtValueChars<uint8> == 3);
static_assert(AtValueChars<uint16> == 5);
static_assert(AtValueChars<uint32> == 10);
static_assert(AtValueChars<bool> == 1);
static_assert(AtValueChars<String<32>> == 66);


} // namespace


int main() {
	TestIntegers();
	TestBool();
	TestStrings();
	TestWriters();
	TestRoundTrip();

	if (failureCount != 0) {
		std::fprintf(stderr, "TestAtValue: %d failure(s)\n", failureCount);
		return 1;
	}

	std::printf("TestAtValue: all checks passed\n");
	return 0;
}
