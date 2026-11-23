#pragma once
#include <Utilities/TextLiteral.h>


// A message of a failed compile-time check. Declared, never defined: a call from a constant expression is ill-formed, and the
// diagnostic prints the message in the template argument.
template<TextLiteral Message>
void VubError();


// Calls VubError<Message>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message>
constexpr void VubRequire(bool condition) {
	if (!condition) {
		VubError<Message>();
	}
}


// Holds a value as a template argument. A requires clause names it with a member of a block: the clause fails when the member is not
// a constant expression, because the builder reads the members of a block at compile time.
template<auto Value>
struct VubConstant {
};
