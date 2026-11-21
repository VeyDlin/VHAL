#pragma once
#include <DemoUSBComposite/Function/CdcPrint.h>
#include <DemoUSBComposite/Function/CompositeFunctions.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <array>
#include <cstddef>
#include <string_view>


namespace DemoUSBComposite {
	// The command line on the serial port: the device echoes what the host's terminal sends, and a prompt follows each command. A line
	// ends with CR (what a terminal sends for Enter), with LF, or with CR LF, and each of those is one end. The other half of a CR LF or
	// LF CR pair ends nothing, while two CRs or two LFs in a row end two lines. The commands are listed by help (CompositeCommandLine.cpp).
	// They drive the keyboard and the mouse, so the host must have configured the device; help and status work at any time.
	//
	// Process() runs from the main loop. It reads what the port received, runs the lines that ended, and types the text of a type command
	// a few characters per pass, as the keyboard queue has room. Nothing waits.
	class CompositeCommandLine {
	private:
		static constexpr std::size_t LineSize = 80;

		SerialPort& port;
		CdcPrint& out;
		Keyboard& keyboard;
		Mouse& mouse;
		const USBDevice& device;
		const uint32& loopedBytes;
		std::array<char, LineSize> line{};
		std::size_t lineLength = 0;
		// Set when the line had more characters than fit. Such a line is refused when it ends, so a command never runs on a cut line.
		bool lineTooLong = false;
		// The last byte when it was a line end (CR or LF), 0 after any other byte. It is kept across EndLine(), so the second byte of a
		// CR LF or LF CR pair is recognised.
		uint8 lastLineEnd = 0;
		bool terminalWasOpen = false;
		// The text of the type command, with a NUL after its last character. typeLength is 0 when no text is being typed.
		std::array<char, LineSize + 1> typeText{};
		std::size_t typeLength = 0;
		std::size_t typeOffset = 0;


	public:
		CompositeCommandLine(
			SerialPort& serialPort,
			CdcPrint& serialOut,
			Keyboard& keyboardFunction,
			Mouse& mouseFunction,
			const USBDevice& usbDevice,
			const uint32& looped
		)
			: port(serialPort),
			  out(serialOut),
			  keyboard(keyboardFunction),
			  mouse(mouseFunction),
			  device(usbDevice),
			  loopedBytes(looped) {
		}


		// Called from the main loop. A terminal that opens gets the greeting and a prompt.
		void Process() {
			bool open = port.IsTerminalOpen();
			if (open && !terminalWasOpen) {
				ResetLine();
				out.WriteLine("USB composite demo. Type help for the commands.");
				Prompt();
			}

			terminalWasOpen = open;
			FeedTyping();
			ReadInput();
		}


	private:
		void ReadInput();
		void HandleByte(uint8 value);
		void EndLine();
		void ResetLine();
		void Prompt();
		void Execute(std::string_view text);
		void FeedTyping();
		bool RequireConfigured();
		void Help(std::string_view arguments);
		void Type(std::string_view arguments);
		void Key(std::string_view arguments);
		void Move(std::string_view arguments);
		void Click(std::string_view arguments);
		void Wheel(std::string_view arguments);
		void Status(std::string_view arguments);
	};
}
