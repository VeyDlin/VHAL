// expect: CompileTime: the first value is below 10
#include <Utilities/CompileTime/Require.h>


constexpr int CheckedValue(int value) {
	RequireValue<"CompileTime: the first value is below 10", 10>(value < 10);
	return value;
}


constexpr int result = CheckedValue(12);
