#pragma once
#include <Utilities/TextLiteral.h>


// Compile-time checks of the command line declarations. A message is carried by a template argument, and a failed check calls
// CommandError<Message>(), which is declared but never defined. The compiler then reports the call as an error and prints the
// message in the template argument, the same way the USB descriptor builders and the FAT12 builder report their rules.


// Declared, never defined. A call from a constant expression is ill-formed, and the diagnostic prints the message.
template<TextLiteral Message>
void CommandError();


// Calls CommandError<Message>() when the condition does not hold. Used only inside constant expressions.
template<TextLiteral Message>
constexpr void CommandRequire(bool condition) {
	if (!condition) {
		CommandError<Message>();
	}
}
