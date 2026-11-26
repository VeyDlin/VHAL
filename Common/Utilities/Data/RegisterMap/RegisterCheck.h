#pragma once
#include <Utilities/CompileTime/TextLiteral.h>


// Compile-time checks of the register map. A failed check calls RegisterError<Message>(), which is declared but never defined.
// A call from a constant expression is then an error, and the compiler prints the message from the template argument.
// Outside a constant expression the same call is a link error, so the checks belong in constexpr builders and constants.


// Declared, never defined.
template<TextLiteral Message>
void RegisterError();


// Calls RegisterError<Message>() when the condition does not hold.
template<TextLiteral Message>
constexpr void RegisterRequire(bool condition) {
	if (!condition) {
		RegisterError<Message>();
	}
}
