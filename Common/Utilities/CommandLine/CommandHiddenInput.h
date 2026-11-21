#pragma once
#include "CommandArgument.h"
#include "CommandModules.h"
#include "CommandTypes.h"
#include <cstddef>
#include <string_view>


// The HiddenInput module. A Secret argument takes text like a String, but each character is echoed as '*', so a password typed at the
// console does not show on the screen:
//
//   using App = CommandLine<AppCommands, AppContext, Help, HiddenInput>;
//
//   .Command("login", "log in", &Login, Argument<String<16>>("user"), Argument<Secret<16>>("password"))
//   ResultStatus Login(CommandCall<AppContext> call, String<16> user, Secret<16> password);
//
// Backspace erases one '*' as it erases one character. A Secret argument may be quoted, as a String is. The echo is the console's, so the
// module is a feature rather than a hook: a set with a Secret argument needs this module listed, a compile error otherwise.
//
// Inside the quotes of a secret every character is echoed as '*', the quotes and the spaces too, so the length and the spaces of a quoted
// secret do not show. Only the space that ends the argument is echoed as a space.
//
// After a name that matches no command, the rest of the line is hidden, spaces too, so a password typed after a misspelt name is not
// echoed. Past the last argument of a command with a Secret, the text is hidden too, spaces excepted, since it may be the rest of a
// password typed with a space in it. A line that starts with help is not hidden when Help is listed, since its words are not secrets, so
// after the word help a secret is echoed as typed.
//
// What still shows: the spaces that end an argument, so the number of words shows; the length of a secret, one '*' per character; and a
// secret typed after the word help, as above.
//
// A line that holds a secret is not kept by History, and LineEditing draws the tail of such a line as '*' too, so a secret that a cursor
// move brings back to the screen is not printed. A secret typed and then erased still makes its line a secret line, so the line is not kept.
//
// A Secret has no default. Its value is read and passed to the handler as a Secret<N>, which holds the characters as a String<N> does.


// The text of a secret. It is a String, so the handler reads the characters with View().
template<std::size_t Capacity>
struct Secret : String<Capacity> {
};


// A secret argument of at most Capacity characters.
template<std::size_t Capacity>
class Argument<Secret<Capacity>> {
public:
	static constexpr CommandArgumentKind Kind = CommandArgumentKind::Secret;
	static constexpr std::size_t MaxLength = Capacity;
	using Value = Secret<Capacity>;

	std::string_view name;
	Secret<Capacity> defaultValue{};
	bool hasDefault = false;


	constexpr explicit Argument(std::string_view argumentName) : name(argumentName) {
	}


	constexpr void Verify() const {
	}
};


// The reader of a secret: a text reader that echoes its characters as '*'.
template<std::size_t Capacity>
struct CommandSlotOf<Argument<Secret<Capacity>>> {
	using Type = CommandTextSlot<Capacity>;
};


struct HiddenInput {
	using Provides = CommandHiddenFeature;
};
