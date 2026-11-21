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
// byte like any other, so Process cancels the line at the place where the byte came. A Ctrl+C that finds the ring full is refused, and
// the ring keeps its place: Process cancels the line after the bytes accepted before it, as if it had been queued.


template<typename Set, typename App, typename... Modules>
class CommandLine {
	static_assert((CommandModuleReady<Modules, Modules...>() && ...), "a module needs a feature that no listed module provides");
	static_assert((0 + ... + (CommandIsInputRing<Modules> ? 1 : 0)) <= 1, "a console lists at most one InputRing");

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
	CommandInput input;
	CommandParser<Set> parser;
	CommandPack<Modules...> modules{};


public:
	CommandLine(App& application, Print& output) : app(application), out(output) {
		Set::template CheckContext<App>();
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

		uint8 byte = 0;
		for (;;) {
			if (ring.InterruptDue()) {
				Interrupted();
			}

			if (!ring.Pop(byte)) {
				return;
			}

			Handle(byte);
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


private:
	void SyncTerminal() {
		bool open = terminalOpen.load(std::memory_order_acquire);
		if (open == reportedOpen) {
			return;
		}

		reportedOpen = open;
		parser.Reset();
		input.Reset();

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

		if (byte == '\t') {
			Accept(' ');
			return;
		}

		if (byte < 0x20 || byte > 0x7E) {
			return;
		}

		Accept(character);
	}


	// Reads a printable character, raises the Word event it ends (default-only build), and echoes it.
	void Accept(char character) {
		if constexpr (Set::EntryCount == 0) {
			if (parser.WordEnds(character)) {
				RaiseWord();
			}
		}

		parser.Push(character);
		out.WriteRaw(&character, 1);
	}


	void Erase() {
		if (parser.Backspace()) {
			out.Write("\b \b");
		}
	}


	// Ctrl+C: the line is cancelled, and the Interrupt event goes out. A long command is cancelled by its module, through the event.
	void Interrupted() {
		parser.Reset();
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

		parser.Reset();
		Prompt();
	}


	void EndWordLine() {
		if (!parser.HasWord()) {
			RaiseMessage(CommandEventKind::EmptyLine);
			return;
		}

		if (parser.WordLength() > 0) {
			RaiseWord();
		}

		RaiseMessage(CommandEventKind::EndOfLine);
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


	// Calls the command with its values. A status other than ok is raised as CommandFailed, with the command's name.
	void RunCommand(CommandEvent& event) {
		ResultStatus status = ResultStatus::notFound;

		parser.WithLevel([&]<typename L>() {
			status = L::Invoke(parser.Command(), app, out, parser.Values());
		});

		if (status != ResultStatus::ok) {
			event.kind = CommandEventKind::CommandFailed;
			event.status = status;
			Raise(event);
		}
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
			CommandWriteEvent(out, event);
		}
	}


	// The console prints the prompt after each line and after the greeting, whatever the handlers print.
	void Prompt() {
		ModulePrompt();
		out.Write("> ");
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


	void ModulePrompt() {
		CommandPackForEach(modules, [&]<typename Module>(Module& module) {
			CommandCallPrompt(module, *this);
		});
	}
};
