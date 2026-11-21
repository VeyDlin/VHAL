#include <DemoUSBComposite/CommandLine/CompositeCommandLine.h>
#include <DemoProducts.h>
#include <DemoUSBComposite/Status/CompositeStatus.h>
#include <algorithm>
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


	// The largest absolute value of a move or wheel argument. A larger value is refused, so one command cannot queue an unbounded amount
	// of movement.
	static constexpr int32 MaxAxisValue = 32767;


	// The parity and stop bit names of the status line, indexed by the codes of USBCdcLineCoding.
	static constexpr const char* ParityNames[] = { "none", "odd", "even", "mark", "space" };
	static constexpr const char* StopBitNames[] = { "1", "1.5", "2" };


	static constexpr const char* HelpLines[] = {
		"commands:",
		"  help                       this list",
		"  type <text>                types the text on a US keyboard, printable ASCII only",
		"  key <name>                 presses and releases one key: enter esc backspace tab space up down left right f13..f24",
		"  move <dx> <dy>             moves the mouse by dx and dy counts, negative for left and up",
		"  click [left|right|middle]  clicks a mouse button, left by default",
		"  wheel <n>                  scrolls n steps, positive away from you",
		"  status                     USB state, keyboard LEDs, line coding, DTR and the vendor loop count",
	};


	// Takes the next word of text: the characters up to the next space, after any spaces before it. text keeps what follows the word.
	static std::string_view TakeWord(std::string_view& text) {
		while (!text.empty() && text.front() == ' ') {
			text.remove_prefix(1);
		}

		std::size_t end = text.find(' ');
		std::string_view word = text.substr(0, end);
		text.remove_prefix(word.size());
		return word;
	}


	static bool IsBlank(std::string_view text) {
		for (char character : text) {
			if (character != ' ') {
				return false;
			}
		}

		return true;
	}


	// Reads a decimal number with an optional sign. It fails for anything else and for a value beyond MaxAxisValue.
	static bool ParseNumber(std::string_view word, int32& value) {
		bool negative = false;
		if (!word.empty() && (word.front() == '-' || word.front() == '+')) {
			negative = word.front() == '-';
			word.remove_prefix(1);
		}

		if (word.empty() || word.size() > 5) {
			return false;
		}

		int32 result = 0;
		for (char digit : word) {
			if (digit < '0' || digit > '9') {
				return false;
			}

			result = result * 10 + (digit - '0');
		}

		if (result > MaxAxisValue) {
			return false;
		}

		value = negative ? -result : result;
		return true;
	}


	static bool FindKey(std::string_view name, uint8& keyCode) {
		for (const KeyName& entry : KeyNames) {
			if (entry.name == name) {
				keyCode = entry.keyCode;
				return true;
			}
		}

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


	// The result of a move or a wheel command. busy is not an error: the movement was taken and waits in the mouse until the queue has room,
	// and the main loop sends it. outOfRange is an error: the movement was not taken, because the pending movement is at its limit.
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


	void CompositeCommandLine::ReadInput() {
		std::array<uint8, 32> received{};
		uint16 count = port.Read(received);
		for (uint16 index = 0; index < count; index++) {
			HandleByte(received[index]);
		}
	}


	void CompositeCommandLine::HandleByte(uint8 value) {
		if (value == '\r' || value == '\n') {
			uint8 other = value == '\r' ? '\n' : '\r';
			// The second half of a CR LF or LF CR pair ends nothing: the first byte already ended the line.
			if (lastLineEnd == other) {
				lastLineEnd = 0;
				return;
			}

			lastLineEnd = value;
			EndLine();
			return;
		}

		lastLineEnd = 0;

		// BS and DEL both erase the last character. The terminal shows the erase by moving back, blanking the cell and moving back again.
		if (value == 0x08 || value == 0x7F) {
			if (lineLength > 0) {
				lineLength--;
				out.Write("\b \b");
			}

			return;
		}

		// Printable ASCII only. Other bytes, such as a tab, are dropped without an echo.
		if (value < 0x20 || value > 0x7E) {
			return;
		}

		if (lineLength == line.size()) {
			lineTooLong = true;
			return;
		}

		line[lineLength] = static_cast<char>(value);
		lineLength++;
		out.Write(&line[lineLength - 1], 1);
	}


	// A line of spaces only is an empty line: it gets a new prompt and no error.
	void CompositeCommandLine::EndLine() {
		out.Line();
		if (lineTooLong) {
			out.WriteLine("error: the line is longer than 80 characters");
		} else if (!IsBlank(std::string_view(line.data(), lineLength))) {
			Execute(std::string_view(line.data(), lineLength));
		}

		ResetLine();
		Prompt();
	}


	void CompositeCommandLine::ResetLine() {
		lineLength = 0;
		lineTooLong = false;
	}


	void CompositeCommandLine::Prompt() {
		out.Write("> ");
	}


	void CompositeCommandLine::Execute(std::string_view text) {
		std::string_view arguments = text;
		std::string_view word = TakeWord(arguments);
		// The one space after the command word is not an argument. Any further spaces are kept, so type sends them.
		if (!arguments.empty()) {
			arguments.remove_prefix(1);
		}

		if (word == CompositeCommands::Help) {
			Help(arguments);
		} else if (word == CompositeCommands::Type) {
			Type(arguments);
		} else if (word == CompositeCommands::Key) {
			Key(arguments);
		} else if (word == CompositeCommands::Move) {
			Move(arguments);
		} else if (word == CompositeCommands::Click) {
			Click(arguments);
		} else if (word == CompositeCommands::Wheel) {
			Wheel(arguments);
		} else if (word == CompositeCommands::Status) {
			Status(arguments);
		} else {
			out.Write("error: unknown command '");
			out.Write(word);
			out.WriteLine("', try help");
		}
	}


	// Types the pending text as far as the keyboard queue has room. The text is validated when it is taken, so every character has a key
	// and the typing never stops on a character it cannot send. Pending text is dropped when the host is not configured, so it does not
	// type into the next connection.
	void CompositeCommandLine::FeedTyping() {
		if (typeLength == 0) {
			return;
		}

		if (device.GetState() != USBDeviceState::Configured) {
			typeLength = 0;
			typeOffset = 0;
			return;
		}

		typeOffset += keyboard.TypeText(typeText.data() + typeOffset);
		if (typeOffset == typeLength) {
			typeLength = 0;
			typeOffset = 0;
		}
	}


	bool CompositeCommandLine::RequireConfigured() {
		if (device.GetState() == USBDeviceState::Configured) {
			return true;
		}

		out.WriteLine("error: the USB host has not configured the device yet");
		return false;
	}


	void CompositeCommandLine::Help(std::string_view arguments) {
		if (!IsBlank(arguments)) {
			out.WriteLine("error: help takes no arguments");
			return;
		}

		for (const char* text : HelpLines) {
			out.WriteLine(text);
		}
	}


	// The text is the rest of the line after the command word and one space, so the spaces inside it are typed as well.
	void CompositeCommandLine::Type(std::string_view arguments) {
		if (arguments.empty()) {
			out.WriteLine("error: type needs text, for example: type hello");
			return;
		}

		if (typeLength != 0) {
			out.WriteLine("error: still typing the earlier text, try again");
			return;
		}

		USBHidKeyStroke stroke;
		for (char character : arguments) {
			if (!USBHidAsciiToKey(character, stroke)) {
				out.WriteLine("error: type takes printable ASCII only");
				return;
			}
		}

		if (!RequireConfigured()) {
			return;
		}

		std::copy(arguments.begin(), arguments.end(), typeText.begin());
		typeText[arguments.size()] = '\0';
		typeLength = arguments.size();
		typeOffset = 0;
		out.WriteLine("ok");
	}


	void CompositeCommandLine::Key(std::string_view arguments) {
		std::string_view name = TakeWord(arguments);
		if (name.empty() || !IsBlank(arguments)) {
			out.WriteLine("error: key takes one key name, for example: key enter");
			return;
		}

		uint8 keyCode = 0;
		if (!FindKey(name, keyCode)) {
			out.Write("error: unknown key '");
			out.Write(name);
			out.WriteLine("'");
			return;
		}

		if (!RequireConfigured()) {
			return;
		}

		WriteOutcome(out, keyboard.Tap(keyCode, 0));
	}


	// A move the queue has no room for stays pending in the mouse and goes out from the main loop, so the answer is ok.
	void CompositeCommandLine::Move(std::string_view arguments) {
		int32 dx = 0;
		int32 dy = 0;
		std::string_view dxWord = TakeWord(arguments);
		std::string_view dyWord = TakeWord(arguments);
		if (!ParseNumber(dxWord, dx) || !ParseNumber(dyWord, dy) || !IsBlank(arguments)) {
			out.WriteLine("error: move takes two numbers, dx and dy, from -32767 to 32767");
			return;
		}

		if (!RequireConfigured()) {
			return;
		}

		WriteMotionOutcome(out, mouse.Move(dx, dy));
	}


	void CompositeCommandLine::Click(std::string_view arguments) {
		std::string_view name = TakeWord(arguments);
		uint8 button = Mouse::ButtonLeft;
		if (name == "right") {
			button = Mouse::ButtonRight;
		} else if (name == "middle") {
			button = Mouse::ButtonMiddle;
		} else if (!name.empty() && name != "left") {
			out.Write("error: unknown button '");
			out.Write(name);
			out.WriteLine("', use left, right or middle");
			return;
		}

		if (!IsBlank(arguments)) {
			out.WriteLine("error: click takes one button name at most");
			return;
		}

		if (!RequireConfigured()) {
			return;
		}

		WriteOutcome(out, mouse.Click(button));
	}


	void CompositeCommandLine::Wheel(std::string_view arguments) {
		int32 steps = 0;
		if (!ParseNumber(TakeWord(arguments), steps) || !IsBlank(arguments)) {
			out.WriteLine("error: wheel takes one number of steps, from -32767 to 32767");
			return;
		}

		if (!RequireConfigured()) {
			return;
		}

		WriteMotionOutcome(out, mouse.Scroll(steps));
	}


	void CompositeCommandLine::Status(std::string_view arguments) {
		if (!IsBlank(arguments)) {
			out.WriteLine("error: status takes no arguments");
			return;
		}

		WriteUsbState(out, device, keyboard);
		out.Line();
		USBCdcLineCoding coding = port.GetLineCoding();
		out.Write("baud=");
		out.Write(coding.dteRate);
		out.Write(" data=");
		out.Write(static_cast<uint32>(coding.dataBits));
		out.Write(" parity=");
		out.Write(ParityNames[std::min<std::size_t>(coding.parity, std::size(ParityNames) - 1)]);
		out.Write(" stop=");
		out.Write(StopBitNames[std::min<std::size_t>(coding.stopBits, std::size(StopBitNames) - 1)]);
		out.Line();
		out.Write("dtr=");
		out.Write(port.IsTerminalOpen());
		out.Write(" looped=");
		out.Write(loopedBytes);
		out.Write(" cdcDropped=");
		out.Write(out.GetDropped());
		out.Write(" usbErr=");
		out.Write(device.GetErrorCount());
		out.Line();
	}
}
