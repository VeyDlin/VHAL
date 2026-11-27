#pragma once
// Checks of the host tests. A check that holds only counts. A check that fails prints its file, line, expression and values to
// stderr and counts as a failure. A test program ends with return VhalCheck::Finish("<test name>"): the summary goes to stdout and
// the result is 0 when every check held, 1 when one failed.
//
//   VHAL_CHECK(condition)                    fails when the condition is false
//   VHAL_CHECK_EQ(actual, expected)          integers of any signedness, enums (by their underlying value), bool, and text
//                                            (both sides convertible to std::string_view). Floating-point values and pointers
//                                            other than text are a compile error.
//   VHAL_CHECK_NEAR(actual, expected, tolerance)   |actual - expected| <= tolerance, compared as double
#include <cmath>
#include <cstdio>
#include <string_view>
#include <type_traits>
#include <utility>


#define VHAL_CHECK(condition) ::VhalCheck::ExpectTrue(static_cast<bool>(condition), __FILE__, __LINE__, #condition)

#define VHAL_CHECK_EQ(actual, expected) ::VhalCheck::ExpectEqual(__FILE__, __LINE__, #actual, #expected, (actual), (expected))

#define VHAL_CHECK_NEAR(actual, expected, tolerance) \
	::VhalCheck::ExpectNear( \
		static_cast<double>(actual), \
		static_cast<double>(expected), \
		static_cast<double>(tolerance), \
		__FILE__, \
		__LINE__, \
		#actual, \
		#expected, \
		#tolerance)


namespace VhalCheck {


// The checks run and the checks that failed, over the whole test program.
inline int checkCount = 0;
inline int failCount = 0;


// Text is any type that converts to std::string_view: string literals, const char* and std::string_view.
template<typename T>
concept Text = std::is_convertible_v<const T&, std::string_view>;

// A pointer that is not text. A const char* is text and is accepted by VHAL_CHECK_EQ.
template<typename T>
concept Pointer = std::is_pointer_v<T> && !Text<T>;

// An integer that is not bool, or an enum, which compares by its underlying value.
template<typename T>
concept IntegerLike = (std::is_integral_v<T> && !std::is_same_v<T, bool>) || std::is_enum_v<T>;


// Prints one value: bool as true or false, an integer in its own signedness, an enum by its underlying value, a floating-point
// value with nine significant digits, and text in double quotes.
template<typename T>
inline void PrintValue(std::FILE* stream, const T& value) {
	if constexpr (std::is_same_v<T, bool>) {
		std::fputs(value ? "true" : "false", stream);
	} else if constexpr (std::is_enum_v<T>) {
		PrintValue(stream, std::to_underlying(value));
	} else if constexpr (std::is_integral_v<T>) {
		if constexpr (std::is_signed_v<T>) {
			std::fprintf(stream, "%lld", static_cast<long long>(value));
		} else {
			std::fprintf(stream, "%llu", static_cast<unsigned long long>(value));
		}
	} else if constexpr (std::is_floating_point_v<T>) {
		std::fprintf(stream, "%.9g", static_cast<double>(value));
	} else {
		std::string_view text(value);
		std::fprintf(stream, "\"%.*s\"", static_cast<int>(text.size()), text.data());
	}
}


// The integer that an integer-like value compares as. An enum gives its underlying value. Every integer goes to the 64-bit type of its
// signedness, because std::cmp_equal takes only the standard integer types, and char is not one of them.
template<typename T>
constexpr auto IntegerValue(const T& value) {
	if constexpr (std::is_enum_v<T>) {
		return IntegerValue(std::to_underlying(value));
	} else if constexpr (std::is_signed_v<T>) {
		return static_cast<long long>(value);
	} else {
		return static_cast<unsigned long long>(value);
	}
}


inline void ExpectTrue(bool condition, const char* file, int line, const char* expression) {
	checkCount++;
	if (condition) {
		return;
	}

	failCount++;
	std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
}


template<typename TActual, typename TExpected>
inline void ExpectEqual(
	const char* file,
	int line,
	const char* actualText,
	const char* expectedText,
	const TActual& actual,
	const TExpected& expected) {
	using Actual = std::remove_cvref_t<TActual>;
	using Expected = std::remove_cvref_t<TExpected>;

	bool passed = false;

	if constexpr (std::is_floating_point_v<Actual> || std::is_floating_point_v<Expected>) {
		static_assert(
			!std::is_floating_point_v<Actual> && !std::is_floating_point_v<Expected>,
			"VHAL_CHECK_EQ does not compare floating-point values: use VHAL_CHECK_NEAR");
	} else if constexpr (Pointer<Actual> || Pointer<Expected>) {
		static_assert(
			!Pointer<Actual> && !Pointer<Expected>,
			"VHAL_CHECK_EQ does not compare pointers: use VHAL_CHECK");
	} else if constexpr (Text<Actual> && Text<Expected>) {
		passed = std::string_view(actual) == std::string_view(expected);
	} else if constexpr (std::is_same_v<Actual, bool> && std::is_same_v<Expected, bool>) {
		passed = actual == expected;
	} else if constexpr (IntegerLike<Actual> && IntegerLike<Expected>) {
		passed = std::cmp_equal(IntegerValue(actual), IntegerValue(expected));
	} else {
		static_assert(
			sizeof(Actual) == 0,
			"VHAL_CHECK_EQ compares two integers, two enums, two bools or two texts: use VHAL_CHECK for other types");
	}

	checkCount++;
	if (passed) {
		return;
	}

	failCount++;
	std::fprintf(stderr, "%s:%d: check failed: %s == %s (got ", file, line, actualText, expectedText);
	PrintValue(stderr, actual);
	std::fputs(", expected ", stderr);
	PrintValue(stderr, expected);
	std::fputs(")\n", stderr);
}


inline void ExpectNear(
	double actual,
	double expected,
	double tolerance,
	const char* file,
	int line,
	const char* actualText,
	const char* expectedText,
	const char* toleranceText) {
	checkCount++;
	if (std::fabs(actual - expected) <= tolerance) {
		return;
	}

	failCount++;
	std::fprintf(stderr, "%s:%d: check failed: |%s - %s| <= %s (got ", file, line, actualText, expectedText, toleranceText);
	PrintValue(stderr, actual);
	std::fputs(", expected ", stderr);
	PrintValue(stderr, expected);
	std::fputs(", tolerance ", stderr);
	PrintValue(stderr, tolerance);
	std::fputs(")\n", stderr);
}


// Prints the summary of the test program and returns its exit code.
inline int Finish(const char* testName) {
	if (failCount == 0) {
		std::printf("%s: %d checks passed\n", testName, checkCount);
		return 0;
	}

	std::printf("%s: %d of %d checks failed\n", testName, failCount, checkCount);
	return 1;
}


}
