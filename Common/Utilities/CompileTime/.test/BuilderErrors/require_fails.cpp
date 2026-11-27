// expect: CompileTime: the sum is not negative
#include <Utilities/CompileTime/Require.h>


constexpr int CheckedSum(int first, int second) {
	Require<"CompileTime: the sum is not negative">(first + second >= 0);
	return first + second;
}


constexpr int result = CheckedSum(-4, 1);
