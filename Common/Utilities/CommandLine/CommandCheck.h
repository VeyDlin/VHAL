#pragma once
#include <cstddef>


// Compile-time checks of the command line declarations. A message is carried by a template argument, and a failed check calls
// CommandError<Message>(), which is declared but never defined. The compiler then reports the call as an error and prints the
// message in the template argument, the same way the USB descriptor builders and the FAT12 builder report their rules.


// A message as a template argument.
template<std::size_t Size>
struct CommandText {
	char value[Size];


	constexpr CommandText(const char (&text)[Size]) {
		for (std::size_t index = 0; index < Size; index++) {
			value[index] = text[index];
		}
	}
};


// Declared, never defined. A call from a constant expression is ill-formed, and the diagnostic prints the message.
template<CommandText Message>
void CommandError();


// Calls CommandError<Message>() when the condition does not hold. Used only inside constant expressions.
template<CommandText Message>
constexpr void CommandRequire(bool condition) {
	if (!condition) {
		CommandError<Message>();
	}
}
