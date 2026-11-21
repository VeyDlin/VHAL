#pragma once
#include "CommandInput.h"
#include "CommandMessages.h"
#include "CommandModules.h"
#include "CommandPack.h"
#include "CommandParser.h"
#include "CommandRing.h"
#include "CommandSet.h"
#include "CommandTypes.h"
#include <Utilities/Console/Print.h>
#include <Utilities/DataTypes.h>
#include <atomic>
#include <cstddef>
#include <string_view>


// A text console for one command set. The application feeds it bytes and calls Process from its main loop:
//
//   using AppCommands = CommandSet<+[] {
//       return CommandSetBuilder()
//           .Command("key", "press a key", &Key, Argument<uint8>("code"))
//           .OnInterrupt(&StopMotor)
//           .Default(&OnEvent);
//   }>;
//
//   CommandLine<AppCommands, App> console(app, out);
//   console.Feed(byte);              // from the receive interrupt or the main loop
//   console.Process();               // from the main loop: parses, raises events, runs handlers
//   console.SetTerminalOpen(dtr);
//
// The input ring is 32 bytes unless the console lists an InputRing<N> among its types. Modules are listed after the application type (see
// CommandModules.h).
//
// Everything the console prints comes from Process. Feed calls the OnInterrupt handler at once, when it has one, and queues the Ctrl+C
// byte like any other, so Process cancels the line at the place where the byte came. A Ctrl+C that finds the ring full is refused. The
// ring keeps its place, so Process cancels the line after the bytes accepted before it, as if it had been queued. Two refused presses
// can wait at once. A third press, while two wait, is dropped: it raises no Interrupt and cancels no line, though an OnInterrupt handler
// still runs.
//
// A long command (LongCommands) runs over several Process passes. Its line is still the parser's line, so the parser keeps the command and
// its values until the command ends. Input that arrives meanwhile waits in the ring, up to its size, and is read after the command.


// The run of a long command: its state, which the console keeps while it runs, the name for a failure event, and whether it runs. Without
// LongCommands the run is empty, so the console has no state for long commands.
template<typename Set, bool Enabled>
struct CommandLongRun {
};


template<typename Set>
struct CommandLongRun<Set, true> {
	alignas(Set::MaxStepAlign) std::byte state[CommandArraySize(Set::MaxStepBytes)]{};
	std::string_view name;
	bool running = false;
};


template<typename Set, typename App, typename... Modules>
class CommandLine {
	static constexpr bool LongEnabled = CommandProvided<CommandLongFeature, Modules...>();
	static constexpr bool MasksEcho = CommandProvided<CommandHiddenFeature, Modules...>();
	static constexpr bool LineBuffered = CommandProvided<CommandLineBufferFeature, Modules...>();
	static constexpr bool HelpListed = (CommandModuleKeepsCopy<Modules>() || ...);

	static_assert((CommandModuleReady<Modules, Modules...>() && ...), "a module needs a feature that no listed module provides");
	static_assert((0 + ... + (CommandIsInputRing<Modules> ? 1 : 0)) <= 1, "a console lists at most one InputRing");
	static_assert(CommandHelpFirst<Modules...>(), "Help must be listed before a module that takes characters, keys or events (LineEditing and History are exempt)");
	static_assert(!Set::HasLong || LongEnabled, "a long command needs the LongCommands module, listed in the console");
	static_assert(!Set::HasFloat || CommandProvided<CommandFloatFeature, Modules...>(), "a float argument needs the FloatArguments module, listed in the console");
	static_assert(!Set::HasSecret || MasksEcho, "a Secret argument needs the HiddenInput module, listed in the console");

public:
	// The command set of the console, for modules that look up commands (Help).
	using Commands = Set;


private:
	static constexpr std::size_t RingSize = CommandRingSizeOf<Modules...>::Value;

	App& app;
	Print& out;
	CommandRing<RingSize> ring;
	std::atomic<bool> terminalOpen{false};
	bool reportedOpen = false;
	uint32 changes = 0;
	CommandInput input;
	CommandParser<Set> parser;
	CommandPack<Modules...> modules{};
	[[no_unique_address]] CommandLongRun<Set, LongEnabled> longRun{};


public:
	CommandLine(App& application, Print& output) : app(application), out(output) {
		Set::template CheckContext<App>();

		if constexpr (HelpListed) {
			parser.AllowHelp();
		}
	}


	// Takes one byte of input. Safe to call from an interrupt.
	void Feed(uint8 byte) {
		if constexpr (Set::HasInterrupt) {
			if (byte == CommandInterruptByte) {
				Set::CallInterrupt(app);
			}
		}

		if (!ring.Push(byte) && byte == CommandInterruptByte) {
			ring.RefuseInterrupt();
		}
	}


	// Reads the queued input, parses it, raises its events and runs its commands. Call it from the main loop.
	void Process() {
		SyncTerminal();

		if (ring.TakeLost() > 0) {
			RaiseMessage(CommandEventKind::InputOverflow);
		}

		if (LongPass()) {
			return;
		}

		uint8 byte = 0;
		for (;;) {
			while (ring.InterruptDue()) {
				Interrupted();
			}

			if (ring.Pop(byte)) {
				Handle(byte);

				if (LongRunning()) {
					return;
				}

				continue;
			}

			// Pop also returns false at the place of a press that is due now, so that Interrupt comes before the byte there.
			if (!ring.InterruptDue()) {
				return;
			}

			Interrupted();
		}
	}


	// Opens or closes the terminal. The events come from the next Process.
	void SetTerminalOpen(bool open) {
		terminalOpen.store(open, std::memory_order_release);
	}


	// For modules: the console's output and application.
	Print& Output() {
		return out;
	}


	App& Application() {
		return app;
	}


	// Types one character through the modules, as if it came from the input. Completion uses it to type what it completes.
	void Type(char character) {
		Character(static_cast<uint8>(character));
	}


	// Adds a printable character at the end of the line: the parser reads it and it is echoed. A module that keeps the line calls this.
	void Append(char character) {
		Accept(character);
	}


	// Removes the last character of the line, when the parser has one, and echoes the erase. Returns false when nothing is removed.
	bool EraseLast() {
		return Erase();
	}


	// Parses text again, from the start of the line, without echo and without events. The screen is the module's to redraw.
	void Replay(const char* text, std::size_t size) {
		parser.Reset();

		for (std::size_t index = 0; index < size; index++) {
			parser.Push(text[index]);
		}

		changes++;
	}


	// Writes the prompt again, with the modules' prompt hooks. Completion uses it after it lists the choices.
	void RedrawPrompt() {
		Prompt();
	}


	// Writes text that the line shows, for a module that redraws the line. A line with a secret shows '*' for each character, so no secret
	// is printed again.
	void Echo(std::string_view text) {
		if (parser.HoldsSecret()) {
			for (std::size_t index = 0; index < text.size(); index++) {
				out.Write("*");
			}

			return;
		}

		out.Write(text);
	}


	// Whether the line holds a Secret argument's text. A module that keeps history does not keep such a line.
	bool HoldsSecret() const {
		return parser.HoldsSecret();
	}


	// Counts the changes to the line. A module compares it to tell whether the line changed since its last look.
	uint32 Changes() const {
		return changes;
	}


	// The parser, for a module that reads the token at the end of the line.
	const CommandParser<Set>& Parsed() const {
		return parser;
	}


	// Calls visitor with the first listed module that provides Feature. Returns false when no listed module provides it.
	template<typename Feature, typename Visitor>
	bool WithModule(Visitor&& visitor) {
		bool found = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			if constexpr (CommandModuleProvides<Feature, Module>()) {
				if (!found) {
					visitor(module);
					found = true;
				}
			}
		});

		return found;
	}


private:
	void SyncTerminal() {
		bool open = terminalOpen.load(std::memory_order_acquire);
		if (open == reportedOpen) {
			return;
		}

		reportedOpen = open;

		if constexpr (LongEnabled) {
			if (longRun.running) {
				StepLong(CommandStepMode::Cancel);
				EndLong();
			}
		}

		parser.Reset();
		input.Reset();
		changes++;

		if (open) {
			RaiseMessage(CommandEventKind::TerminalOpened);
			Prompt();
		} else {
			RaiseMessage(CommandEventKind::TerminalClosed);
		}
	}


	void Handle(uint8 byte) {
		CommandInputStep step = input.Step(byte);

		switch (step.kind) {
			case CommandInputKind::Ignore:
				return;
			case CommandInputKind::Character:
				Character(step.byte);
				return;
			case CommandInputKind::LineEnd:
				EndLine();
				return;
			case CommandInputKind::Key:
				ModuleKey(step.key);
				return;
		}
	}


	void Character(uint8 byte) {
		char character = static_cast<char>(byte);

		if (ModuleCharacter(character)) {
			return;
		}

		if (byte == CommandInterruptByte) {
			Interrupted();
			return;
		}

		if (byte == 0x08 || byte == 0x7F) {
			Erase();
			return;
		}

		// A Tab that no module takes is typed as a space, through the modules, so a module that keeps the line sees the space.
		if (byte == '\t') {
			Character(' ');
			return;
		}

		if (byte < 0x20 || byte > 0x7E) {
			return;
		}

		Accept(character);
	}


	// Reads a printable character, and echoes it. A character of a Secret argument is echoed as '*'. In a default-only build without a line
	// buffer, the Word event that the character ends is raised here. With LineBuffered, the Word events come at the line end (EndWordLine).
	void Accept(char character) {
		if constexpr (Set::EntryCount == 0 && !LineBuffered) {
			if (parser.WordEnds(character)) {
				RaiseWord();
			}
		}

		bool masked = Masked(character);

		parser.Push(character);
		changes++;

		if (masked) {
			out.Write("*");
		} else {
			out.WriteRaw(&character, 1);
		}
	}


	bool Masked(char character) const {
		if constexpr (MasksEcho) {
			return parser.HidesNext(character);
		} else {
			static_cast<void>(character);
			return false;
		}
	}


	// Erases the last character of the line, and echoes the erase. In a default-only build with a line buffer the buffer decides, because
	// the parser cannot go back over a word that has been ended; the parser is read again at the line end.
	bool Erase() {
		bool erased = parser.Backspace();

		if constexpr (Set::EntryCount == 0 && LineBuffered) {
			erased = true;
		}

		if (!erased) {
			return false;
		}

		changes++;
		out.Write("\b \b");
		return true;
	}


	// Ctrl+C: the line is cancelled, and the Interrupt event goes out. A long command is cancelled by the console, which raises the same event.
	void Interrupted() {
		parser.Reset();
		changes++;
		RaiseMessage(CommandEventKind::Interrupt);
		Prompt();
	}


	void EndLine() {
		out.Line();
		bool taken = ModuleLineEnd();

		if (!taken) {
			if constexpr (Set::EntryCount == 0) {
				EndWordLine();
			} else {
				EndCommandLine();
			}
		}

		if (LongRunning()) {
			return;
		}

		parser.Reset();
		changes++;
		Prompt();
	}


	// The line end of a default-only build. With a line buffer, the parser reads the edited line again from its start, and each Word event
	// is raised as the streamed path raises it, so the words are the ones the line holds at Enter.
	void EndWordLine() {
		if constexpr (LineBuffered) {
			ReplayWords();
		}

		if (!parser.HasWord()) {
			RaiseMessage(CommandEventKind::EmptyLine);
			return;
		}

		if (parser.WordLength() > 0) {
			RaiseWord();
		}

		RaiseMessage(CommandEventKind::EndOfLine);
	}


	// Reads the line buffer again from its start, and raises the Word events of its words. The parser ends in the state of the whole line.
	void ReplayWords() {
		std::string_view text;

		WithModule<CommandLineBufferFeature>([&]<typename Editor>(Editor& editor) {
			text = editor.Text();
		});

		parser.Reset();

		for (char character : text) {
			if (parser.WordEnds(character)) {
				RaiseWord();
			}

			parser.Push(character);
		}
	}


	void EndCommandLine() {
		CommandEvent event;
		CommandEnd end = parser.Evaluate(event);

		if (end == CommandEnd::Run) {
			RunCommand(event);
		} else {
			Raise(event);
		}
	}


	// Calls the command with its values. A status other than ok is raised as CommandFailed, with the command's name. A long command starts
	// here, and its first step runs now; when the step continues, the line stays open until the command ends.
	void RunCommand(CommandEvent& event) {
		ResultStatus status = ResultStatus::notFound;

		if constexpr (Set::HasLong) {
			CommandStep step = StepLong(CommandStepMode::Start);

			if (step.continues) {
				longRun.name = event.command;
				longRun.running = true;
				return;
			}

			status = step.status;
		} else {
			parser.WithLevel([&]<typename L>() {
				status = L::Invoke(parser.Command(), app, out, parser.Values());
			});
		}

		if (status != ResultStatus::ok) {
			event.kind = CommandEventKind::CommandFailed;
			event.status = status;
			Raise(event);
		}
	}


	// Whether a long command is running. Without LongCommands it never is.
	bool LongRunning() const {
		if constexpr (LongEnabled) {
			return longRun.running;
		} else {
			return false;
		}
	}


	// Runs the next step of a running long command, once per pass. Returns true while the command still runs, so the input waits. Ctrl+C
	// cancels the command: its handler gets the cancel call, and the bytes before the Ctrl+C are dropped with it. An accepted Ctrl+C
	// before a waiting refused one cancels here, and the refused press keeps its own place and its own Interrupt.
	bool LongPass() {
		if constexpr (LongEnabled) {
			if (!longRun.running) {
				return false;
			}

			if (ring.DiscardTo(CommandInterruptByte)) {
				CancelLong();
				return false;
			}

			if (ring.InterruptWaiting()) {
				ring.TakeWaitingInterrupt();
				CancelLong();
				return false;
			}

			CommandStep step = StepLong(CommandStepMode::Continue);

			if (step.continues) {
				return true;
			}

			FinishLong(step.status);
			return false;
		} else {
			return false;
		}
	}


	// Calls the long command's handler with the given mode, in the parser's line.
	CommandStep StepLong(CommandStepMode mode) {
		CommandStep step = ResultStatus::ok;

		if constexpr (LongEnabled) {
			parser.WithLevel([&]<typename L>() {
				step = L::Step(parser.Command(), app, out, parser.Values(), longRun.state, mode);
			});
		} else {
			static_cast<void>(mode);
		}

		return step;
	}


	// Ends the run: the parser forgets the line, so the next input starts a new one.
	void EndLong() {
		if constexpr (LongEnabled) {
			longRun.running = false;
		}

		parser.Reset();
		changes++;
	}


	void CancelLong() {
		StepLong(CommandStepMode::Cancel);
		EndLong();
		RaiseMessage(CommandEventKind::Interrupt);
		Prompt();
	}


	void FinishLong(ResultStatus status) {
		EndLong();

		if (status != ResultStatus::ok) {
			CommandEvent event;
			event.kind = CommandEventKind::CommandFailed;
			event.status = status;

			if constexpr (LongEnabled) {
				event.command = longRun.name;
			}

			Raise(event);
		}

		Prompt();
	}


	void RaiseWord() {
		CommandEvent event;
		event.kind = CommandEventKind::Word;
		event.text = parser.WordText();
		event.truncated = parser.WordTruncated();
		Raise(event);
	}


	void RaiseMessage(CommandEventKind kind) {
		CommandEvent event;
		event.kind = kind;
		Raise(event);
	}


	// An event goes to a module that takes it, and otherwise to the set's Default(), or to the standard handler when the set has none.
	void Raise(const CommandEvent& event) {
		if (ModuleEvent(event)) {
			return;
		}

		if constexpr (Set::HasDefault) {
			Set::CallDefault(app, out, event);
		} else {
			CommandWriteEvent<Set>(out, event);
		}
	}


	// The console prints the prompt after each line and after the greeting, whatever the handlers print. A module that writes the prompt
	// itself replaces the "> ".
	void Prompt() {
		if (!ModulePrompt()) {
			out.Write("> ");
		}
	}


	bool ModuleCharacter(char character) {
		bool consumed = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			if (!consumed) {
				consumed = CommandCallCharacter(module, *this, character);
			}
		});

		return consumed;
	}


	bool ModuleKey(CommandKey key) {
		bool consumed = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			if (!consumed) {
				consumed = CommandCallKey(module, *this, key);
			}
		});

		return consumed;
	}


	// Every module gets the line end, so each can reset its own state. Returns true when a module took the line.
	bool ModuleLineEnd() {
		bool taken = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			taken = CommandCallLineEnd(module, *this) || taken;
		});

		return taken;
	}


	bool ModuleEvent(const CommandEvent& event) {
		bool consumed = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			if (!consumed) {
				consumed = CommandCallEvent(module, *this, event);
			}
		});

		return consumed;
	}


	// Returns true when a module wrote the prompt.
	bool ModulePrompt() {
		bool taken = false;

		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			taken = CommandCallPrompt(module, *this) || taken;
		});

		return taken;
	}
};
