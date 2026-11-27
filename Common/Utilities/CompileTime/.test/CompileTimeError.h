#pragma once
#include <Utilities/CompileTime/Require.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>


// Host definitions of the compile-time error functions. A host program that runs a builder at run time calls them, and the calls
// must link. A failed check at run time prints its message and stops the program. A constant expression that reaches these
// definitions does not compile, as on the target, because the functions are not constexpr.


template<TextLiteral Message>
void CompileTimeError() {
	std::fprintf(stderr, "%.*s\n", static_cast<int>(Message.GetLength()), Message.value);
	std::abort();
}


template<TextLiteral Message, std::size_t Value>
void CompileTimeErrorValue() {
	std::fprintf(stderr, "%.*s (value %zu)\n", static_cast<int>(Message.GetLength()), Message.value, Value);
	std::abort();
}
