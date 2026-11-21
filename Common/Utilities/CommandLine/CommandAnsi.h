#pragma once
#include "CommandMessages.h"
#include "CommandModules.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>


// The Ansi module. The console writes ANSI escape sequences for colour and for the screen:
//
//   using App = CommandLine<AppCommands, AppContext, Help, LineEditing<64>, Ansi>;
//
//   The prompt is green. The errors that the standard text reports (CommandFailed, UnknownCommand, ArgumentError and InputOverflow) are red.
//   Ctrl+L (0x0C) clears the screen, and writes the prompt and the line again. Without LineEditing the line is not written again, because the
//   console keeps no text.
//
// The red errors are the standard texts. A set with its own Default() prints its own texts, so Ansi colours nothing it prints, and the
// prompt is the only coloured text. Ansi must be listed after Help, as every module that takes characters, keys or events (see
// CommandModules.h). A console without Ansi writes no escape sequence for colour; the escape sequences of the input are still read and
// dropped, as they always are.


struct Ansi {
	// Ctrl+L: the screen is cleared, and the prompt and the line are written again at the top.
	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		if (character != '\f') {
			return false;
		}

		console.Output().Write("\x1b[2J\x1b[H");
		console.RedrawPrompt();

		console.template WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			editor.Redraw(console);
		});

		return true;
	}


	// The standard error texts, in red. A set with its own Default() gets the events and prints them itself.
	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event) {
		using Set = typename Console::Commands;

		if constexpr (Set::HasDefault) {
			static_cast<void>(console);
			static_cast<void>(event);
			return false;
		} else {
			if (!CommandIsErrorEvent(event)) {
				return false;
			}

			Print& out = console.Output();
			out.Write("\x1b[31m");
			CommandWriteEvent<Set>(out, event);
			out.Write("\x1b[0m");
			return true;
		}
	}


	// The prompt, in green. The console writes no "> " after it.
	template<typename Console>
	bool OnPrompt(Console& console) {
		console.Output().Write("\x1b[32m> \x1b[0m");
		return true;
	}
};
