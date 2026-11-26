#pragma once
#include <Utilities/CompileTime/TextLiteral.h>
#include <cstddef>

namespace VUB {


// A message of a failed compile-time check. Declared, never defined: a call from a constant expression is ill-formed, and the
// diagnostic prints the message in the template argument.
template<TextLiteral Message>
void CheckFailed();


// Calls CheckFailed<Message>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message>
constexpr void Require(bool condition) {
	if (!condition) {
		CheckFailed<Message>();
	}
}


// A failed check that shows a number: the value is a template argument of the diagnostic, so the message can name the value to set.
// Declared, never defined, as CheckFailed.
template<TextLiteral Message, std::size_t Value>
void CheckFailedValue();


// Calls CheckFailedValue<Message, Value>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message, std::size_t Value>
constexpr void RequireValue(bool condition) {
	if (!condition) {
		CheckFailedValue<Message, Value>();
	}
}


// Holds a value as a template argument. A requires clause names it with a member of a block: the clause fails when the member is not
// a constant expression, because the builder reads the members of a block at compile time.
template<auto Value>
struct Constant {
};

}
