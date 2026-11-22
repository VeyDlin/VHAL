#pragma once
#include <DemoProducts.h>
#include <DemoUSBComposite/Function/CdcPrint.h>
#include <DemoUSBComposite/Function/CompositeFunctions.h>
#include <Drivers/Interface/USB/Device/USBDevice.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLongCommands.h>
#include <Utilities/CommandLine/CommandRing.h>
#include <Utilities/CommandLine/CommandSet.h>
#include <cstddef>
#include <string_view>

namespace DemoUSBComposite {


	// The command line on the serial port, built with the command line library (Common/Utilities/CommandLine). The device echoes what the
	// host's terminal sends, and a prompt follows each answer. A line ends with CR, with LF or with CR LF, and each of those is one end.
	// The commands are listed by help (CompositeCommandLine.cpp). They drive the keyboard and the mouse, so the host must have configured
	// the device; help and status work at any time.
	//
	// Process() runs from the main loop. It moves the bytes the port received into the console, runs the lines that ended, and runs the
	// passes of the type command, which types its text a few characters per pass as the keyboard queue has room. Nothing waits.
	class CompositeCommandLine {
	private:
		// The largest absolute value of a move or wheel argument. A larger value is refused, so one command cannot queue an unbounded
		// amount of movement.
		static constexpr int32 MaxAxisValue = 32767;

		// The longest text of the type command, the quotes included.
		static constexpr std::size_t TypeCapacity = 75;
		// The longest key name and mouse button name. A longer one is refused as an argument error.
		static constexpr std::size_t KeyNameCapacity = 16;
		static constexpr std::size_t ButtonNameCapacity = 16;


		// The objects the commands act on. The console keeps a reference to it, so it lives as long as the console does.
		struct Context {
			SerialPort& port;
			CdcPrint& serialOut;
			Keyboard& keyboard;
			Mouse& mouse;
			const USBDevice& device;
			const uint32& loopedBytes;
		};


		// The state of the type command between its passes: whether the first pass ran, and how many characters of the text are typed.
		struct TypeState {
			bool started = false;
			std::size_t typed = 0;
		};


		static ResultStatus HelpCommand(CommandCall<Context> call);
		static CommandStep TypeCommand(CommandLongCall<Context, TypeState> call, String<TypeCapacity> text);
		static ResultStatus KeyCommand(CommandCall<Context> call, String<KeyNameCapacity> name);
		static ResultStatus MoveCommand(CommandCall<Context> call, int32 dx, int32 dy);
		static ResultStatus ClickCommand(CommandCall<Context> call, String<ButtonNameCapacity> button);
		static ResultStatus WheelCommand(CommandCall<Context> call, int32 steps);
		static ResultStatus StatusCommand(CommandCall<Context> call);
		static void OnConsoleEvent(CommandCall<Context> call, const CommandEvent& event);


		// The command words are the ones the PC tool sends (DemoProducts.h).
		using CommandSetType = CommandSet<+[] {
			return CommandSetBuilder()
				.Command(CompositeCommands::Help, "lists the commands", &HelpCommand)
				.Command(CompositeCommands::Type, "types the text on a US keyboard", &TypeCommand, Argument<String<TypeCapacity>>("text"))
				.Command(CompositeCommands::Key, "presses and releases one key", &KeyCommand, Argument<String<KeyNameCapacity>>("name"))
				.Command(
					CompositeCommands::Move,
					"moves the mouse by dx and dy counts",
					&MoveCommand,
					Argument<int32>("dx").Range(-MaxAxisValue, MaxAxisValue),
					Argument<int32>("dy").Range(-MaxAxisValue, MaxAxisValue)
				)
				.Command(CompositeCommands::Click, "clicks a mouse button", &ClickCommand, Argument<String<ButtonNameCapacity>>("button").Default("left"))
				.Command(CompositeCommands::Wheel, "scrolls n steps", &WheelCommand, Argument<int32>("steps").Range(-MaxAxisValue, MaxAxisValue))
				.Command(CompositeCommands::Status, "USB state, keyboard LEDs, line coding, DTR and the vendor loop count", &StatusCommand)
				.Default(&OnConsoleEvent);
		}>;


		// The console: the composite's commands, a ring of 128 bytes for the input (a long command keeps input waiting), and the LongCommands
		// module for the type command.
		using ConsoleType = CommandLine<CommandSetType, Context, InputRing<128>, LongCommands>;


		SerialPort& port;
		Context context;
		ConsoleType console;


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
			  context{serialPort, serialOut, keyboardFunction, mouseFunction, usbDevice, looped},
			  console(context, serialOut) {
		}


		// Called from the main loop: opens or closes the console with the terminal, moves the received bytes into it and runs it.
		void Process() {
			console.SetTerminalOpen(port.IsTerminalOpen());
			ReadInput();
			console.Process();
		}


	private:
		void ReadInput();
	};
}
