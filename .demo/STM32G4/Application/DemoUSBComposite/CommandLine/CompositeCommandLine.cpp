#include <DemoUSBComposite/CommandLine/CompositeCommandLine.h>
#include <DemoUSBComposite/Status/CompositeStatus.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

namespace DemoUSBComposite {


	// A key of the key command: its name and its key code on the Keyboard page of the HID Usage Tables.
	struct KeyName {
		std::string_view name;
		uint8 keyCode;
	};


	// The keys of the key command. F13 to F24 are the function keys no keyboard has, so a program can bind them without a clash with a
	// key a person presses.
	static constexpr KeyName KeyNames[] = {
		{ "enter", 0x28 },
		{ "esc", 0x29 },
		{ "backspace", 0x2A },
		{ "tab", 0x2B },
		{ "space", 0x2C },
		{ "right", 0x4F },
		{ "left", 0x50 },
		{ "down", 0x51 },
		{ "up", 0x52 },
		{ "f13", 0x68 },
		{ "f14", 0x69 },
		{ "f15", 0x6A },
		{ "f16", 0x6B },
		{ "f17", 0x6C },
		{ "f18", 0x6D },
		{ "f19", 0x6E },
		{ "f20", 0x6F },
		{ "f21", 0x70 },
		{ "f22", 0x71 },
		{ "f23", 0x72 },
		{ "f24", 0x73 },
	};


	// The parity and stop bit names of the status line, indexed by the codes of USBCdcLineCoding.
	static constexpr const char* ParityNames[] = { "none", "odd", "even", "mark", "space" };
	static constexpr const char* StopBitNames[] = { "1", "1.5", "2" };


	static constexpr const char* HelpLines[] = {
		"commands:",
		"  help                       this list",
		"  type <text>                types the text on a US keyboard, printable ASCII only; put it in double quotes when it has spaces",
		"  key <name>                 presses and releases one key: enter esc backspace tab space up down left right f13..f24",
		"  move <dx> <dy>             moves the mouse by dx and dy counts, negative for left and up",
		"  click [left|right|middle]  clicks a mouse button, left by default",
		"  wheel <n>                  scrolls n steps, positive away from you",
		"  status                     USB state, keyboard LEDs, line coding, DTR and the vendor loop count",
	};


	static bool FindKey(std::string_view name, uint8& keyCode) {
		for (const KeyName& entry : KeyNames) {
			if (entry.name == name) {
				keyCode = entry.keyCode;
				return true;
			}
		}

		return false;
	}


	// True when every character is printable ASCII, so each one has a key on the US keyboard.
	static bool IsPrintableAscii(std::string_view text) {
		for (char character : text) {
			USBHidKeyStroke stroke;
			if (!USBHidAsciiToKey(character, stroke)) {
				return false;
			}
		}

		return true;
	}


	// Writes the refusal when the device is not configured. False when the command cannot go on.
	static bool RequireConfigured(Print& out, const USBDevice& device) {
		if (device.GetState() == USBDeviceState::Configured) {
			return true;
		}

		out.WriteLine("error: the USB host has not configured the device yet");
		return false;
	}


	// The result of a command that sends a report. The keyboard and the mouse refuse only when the queue has no room.
	static void WriteOutcome(Print& out, ResultStatus status) {
		if (status == ResultStatus::ok) {
			out.WriteLine("ok");
			return;
		}

		if (status == ResultStatus::busy) {
			out.WriteLine("error: the host has not taken the earlier reports yet, try again");
			return;
		}

		out.WriteLine("error: not sent");
	}


	// The result of a move or a wheel command. busy is not an error: the movement is taken and waits in the mouse until the queue has
	// room, and the main loop sends it. outOfRange is an error: the movement is not taken, because the pending movement is at its limit.
	static void WriteMotionOutcome(Print& out, ResultStatus status) {
		if (status == ResultStatus::outOfRange) {
			out.WriteLine("error: the host has not taken the pending movement yet, try again later");
			return;
		}

		if (status == ResultStatus::ok || status == ResultStatus::busy) {
			out.WriteLine("ok");
			return;
		}

		out.WriteLine("error: not sent");
	}


	// The text of an argument error. The commands keep their usage texts. A fault without a text of its own gets a generic one.
	static void WriteArgumentError(Print& out, const CommandEvent& event) {
		if (event.command == "help") {
			out.WriteLine("error: help takes no arguments");
			return;
		}

		if (event.command == "status") {
			out.WriteLine("error: status takes no arguments");
			return;
		}

		if (event.command == "key") {
			if (event.fault == CommandFault::TooLong) {
				out.WriteLine("error: key takes a key name of at most 16 characters");
				return;
			}

			out.WriteLine("error: key takes one key name, for example: key enter");
			return;
		}

		if (event.command == "move") {
			out.WriteLine("error: move takes two numbers, dx and dy, from -32767 to 32767");
			return;
		}

		if (event.command == "wheel") {
			out.WriteLine("error: wheel takes one number of steps, from -32767 to 32767");
			return;
		}

		if (event.command == "click") {
			if (event.fault == CommandFault::TooLong) {
				out.WriteLine("error: click takes a button name of at most 16 characters: left, right or middle");
				return;
			}

			out.WriteLine("error: click takes one button name at most");
			return;
		}

		if (event.command == "type") {
			if (event.fault == CommandFault::Missing) {
				out.WriteLine("error: type needs text, for example: type hello");
				return;
			}

			if (event.fault == CommandFault::Extra) {
				out.WriteLine("error: type takes one word, or the text in double quotes, for example: type \"hello world\"");
				return;
			}

			if (event.fault == CommandFault::TooLong) {
				out.WriteLine("error: type takes at most 75 characters, or 73 between double quotes");
				return;
			}

			if (event.fault == CommandFault::MissingQuote) {
				out.WriteLine("error: type text has no closing double quote");
				return;
			}

			out.WriteLine("error: type text must end at its closing double quote");
			return;
		}

		out.Write("error: ");
		out.Write(event.command);
		out.WriteLine(": the argument is not valid");
	}


	ResultStatus CompositeCommandLine::HelpCommand(CommandCall<Context> call) {
		for (const char* text : HelpLines) {
			call.out.WriteLine(text);
		}

		return ResultStatus::ok;
	}


	// The first pass checks the text and sends the answer, and the later passes type the text as the keyboard queue has room. The
	// text is validated when it is taken, so every character has a key and the typing never stops on a character it cannot send.
	// Text is dropped when the host has deconfigured the device, so it does not type into the next connection.
	// The pass that types the last character returns CommandContinue, and the next pass ends the command. The ending pass types nothing,
	// so the keyboard room of its main loop pass is free for the next command.
	CommandStep CompositeCommandLine::TypeCommand(CommandLongCall<Context, TypeState> call, String<TypeCapacity> text) {
		if (call.cancelled) {
			return ResultStatus::ok;
		}

		if (!call.state.started) {
			if (text.length == 0) {
				call.out.WriteLine("error: type needs text, for example: type hello");
				return ResultStatus::ok;
			}

			if (!IsPrintableAscii(text.View())) {
				call.out.WriteLine("error: type takes printable ASCII only");
				return ResultStatus::ok;
			}

			if (!RequireConfigured(call.out, call.app.device)) {
				return ResultStatus::ok;
			}

			call.state.started = true;
			call.out.WriteLine("ok");
			return CommandContinue;
		}

		if (call.app.device.GetState() != USBDeviceState::Configured) {
			return ResultStatus::ok;
		}

		if (call.state.typed == text.length) {
			return ResultStatus::ok;
		}

		// The buffer ends with a NUL after the last character, because the keyboard reads the text up to its NUL.
		std::array<char, TypeCapacity + 1> buffer{};
		std::copy(text.characters.begin(), text.characters.begin() + static_cast<std::ptrdiff_t>(text.length), buffer.begin());
		call.state.typed += call.app.keyboard.TypeText(buffer.data() + call.state.typed);
		return CommandContinue;
	}


	ResultStatus CompositeCommandLine::KeyCommand(CommandCall<Context> call, String<KeyNameCapacity> name) {
		uint8 keyCode = 0;
		if (!FindKey(name.View(), keyCode)) {
			call.out.Write("error: unknown key '");
			call.out.Write(name.View());
			call.out.WriteLine("'");
			return ResultStatus::ok;
		}

		if (!RequireConfigured(call.out, call.app.device)) {
			return ResultStatus::ok;
		}

		WriteOutcome(call.out, call.app.keyboard.Tap(keyCode, 0));
		return ResultStatus::ok;
	}


	// A move the queue has no room for stays pending in the mouse and goes out from the main loop, so the answer is ok.
	ResultStatus CompositeCommandLine::MoveCommand(CommandCall<Context> call, int32 dx, int32 dy) {
		if (!RequireConfigured(call.out, call.app.device)) {
			return ResultStatus::ok;
		}

		WriteMotionOutcome(call.out, call.app.mouse.Move(dx, dy));
		return ResultStatus::ok;
	}


	ResultStatus CompositeCommandLine::ClickCommand(CommandCall<Context> call, String<ButtonNameCapacity> button) {
		uint8 code = Mouse::ButtonLeft;
		if (button.View() == "right") {
			code = Mouse::ButtonRight;
		} else if (button.View() == "middle") {
			code = Mouse::ButtonMiddle;
		} else if (button.View() != "left") {
			call.out.Write("error: unknown button '");
			call.out.Write(button.View());
			call.out.WriteLine("', use left, right or middle");
			return ResultStatus::ok;
		}

		if (!RequireConfigured(call.out, call.app.device)) {
			return ResultStatus::ok;
		}

		WriteOutcome(call.out, call.app.mouse.Click(code));
		return ResultStatus::ok;
	}


	ResultStatus CompositeCommandLine::WheelCommand(CommandCall<Context> call, int32 steps) {
		if (!RequireConfigured(call.out, call.app.device)) {
			return ResultStatus::ok;
		}

		WriteMotionOutcome(call.out, call.app.mouse.Scroll(steps));
		return ResultStatus::ok;
	}


	ResultStatus CompositeCommandLine::StatusCommand(CommandCall<Context> call) {
		WriteUsbState(call.out, call.app.device, call.app.keyboard);
		call.out.Line();
		USBCdcLineCoding coding = call.app.port.GetLineCoding();
		call.out.Write("baud=");
		call.out.Write(coding.dteRate);
		call.out.Write(" data=");
		call.out.Write(static_cast<uint32>(coding.dataBits));
		call.out.Write(" parity=");
		call.out.Write(ParityNames[std::min<std::size_t>(coding.parity, std::size(ParityNames) - 1)]);
		call.out.Write(" stop=");
		call.out.Write(StopBitNames[std::min<std::size_t>(coding.stopBits, std::size(StopBitNames) - 1)]);
		call.out.Line();
		call.out.Write("dtr=");
		call.out.Write(call.app.port.IsTerminalOpen());
		call.out.Write(" looped=");
		call.out.Write(call.app.loopedBytes);
		call.out.Write(" cdcDropped=");
		call.out.Write(call.app.serialOut.GetDropped());
		call.out.Write(" usbErr=");
		call.out.Write(call.app.device.GetErrorCount());
		call.out.Line();
		return ResultStatus::ok;
	}


	// The events without a command's own handler. The handler prints the greeting, and the console then prints the prompt.
	void CompositeCommandLine::OnConsoleEvent(CommandCall<Context> call, const CommandEvent& event) {
		if (event.kind == CommandEventKind::TerminalOpened) {
			call.out.WriteLine("USB composite demo. Type help for the commands.");
		} else if (event.kind == CommandEventKind::UnknownCommand) {
			call.out.Write("error: unknown command '");
			call.out.Write(event.text);
			call.out.WriteLine("', try help");
		} else if (event.kind == CommandEventKind::ArgumentError) {
			WriteArgumentError(call.out, event);
		} else if (event.kind == CommandEventKind::Interrupt) {
			call.out.WriteLine("^C");
		} else if (event.kind == CommandEventKind::InputOverflow) {
			call.out.WriteLine("input lost");
		}
	}


	void CompositeCommandLine::ReadInput() {
		std::array<uint8, 32> received{};
		uint16 count = port.Read(received);
		for (uint16 index = 0; index < count; index++) {
			console.Feed(received[index]);
		}
	}
}
