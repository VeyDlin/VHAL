#pragma once
#include <Utilities/CompileTime/TextLiteral.h>
#include <cstddef>


// The compile-time checks of the library. A check is a constexpr function that calls CompileTimeError<Message>() when its condition
// does not hold. The function is declared and never defined: a constant expression that reaches it does not compile, and the
// diagnostic prints the message, which is carried by the template argument. A call that runs at run time links to nothing, which
// is the intended failure for a builder that is not evaluated at compile time.
//
// Every message starts with the name of the module that checks it, followed by a colon and a space, as in "AT: name is too long".


// Declared, never defined. See the comment above.
template<TextLiteral Message>
void CompileTimeError();


// Calls CompileTimeError<Message>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message>
constexpr void Require(bool condition) {
	if (!condition) {
		CompileTimeError<Message>();
	}
}


// Declared, never defined. Value is a template argument of the diagnostic, so the message can show the value that failed.
template<TextLiteral Message, std::size_t Value>
void CompileTimeErrorValue();


// Calls CompileTimeErrorValue<Message, Value>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message, std::size_t Value>
constexpr void RequireValue(bool condition) {
	if (!condition) {
		CompileTimeErrorValue<Message, Value>();
	}
}
