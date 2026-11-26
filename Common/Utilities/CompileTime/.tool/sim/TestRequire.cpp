#include <Utilities/CompileTime/Require.h>
#include <Utilities/CompileTime/TextLiteral.h>
#include <Utilities/CompileTime/.tool/sim/CompileTimeError.h>
#include <cstdio>
#include <string_view>


// A constant-evaluated check passes when its condition holds, and the function returns its value.
constexpr int CheckedSum(int first, int second) {
	Require<"CompileTime: the sum is not negative">(first + second >= 0);
	RequireValue<"CompileTime: the first value is below 10", 10>(first < 10);
	return first + second;
}


static_assert(CheckedSum(2, 3) == 5);
static_assert(CheckedSum(0, 0) == 0);


// TextLiteral keeps the length and the text of its literal.
constexpr TextLiteral<6> hello{"hello"};


static_assert(hello.GetLength() == 5);
static_assert(hello.View() == std::string_view("hello"));


// The same functions run at run time when their conditions hold.
int main() {
	int result = CheckedSum(4, 5);
	if (result != 9) {
		std::printf("CheckedSum failed\n");
		return 1;
	}

	if (hello.View() != std::string_view("hello")) {
		std::printf("TextLiteral failed\n");
		return 1;
	}

	std::printf("TestRequire passed\n");
	return 0;
}
