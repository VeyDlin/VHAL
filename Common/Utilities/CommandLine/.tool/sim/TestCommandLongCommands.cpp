#include "CommandSim.h"
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLongCommands.h>
#include <cstddef>
#include <string>


// The LongCommands module, fed one byte at a time. The expected texts are written from the rules in CommandLongCommands.h: a long
// command's first step runs at the Enter, one step runs on each Process pass, and the input waits until the command ends. The set has
// no Default(), so the standard texts are printed: ^C for an interrupt, and the status name for a failed command.


struct LongApp {
	int cancels = 0;
};


struct CountState {
	uint8 done = 0;
};


// Writes a tick on each call until the count is reached. The call after the last tick finishes the command.
CommandStep LongCount(CommandLongCall<LongApp, CountState> call, uint8 times) {
	if (call.cancelled) {
		call.app.cancels++;
		return ResultStatus::ok;
	}

	if (call.state.done == times) {
		return ResultStatus::ok;
	}

	call.state.done++;
	call.out.Write("tick ");
	call.out.Write(call.state.done);
	call.out.Line();
	return CommandContinue;
}


// Continues once, then fails with invalidParameter.
CommandStep LongFail(CommandLongCall<LongApp, CountState> call) {
	if (call.cancelled) {
		return ResultStatus::ok;
	}

	if (call.state.done == 0) {
		call.state.done = 1;
		return CommandContinue;
	}

	return ResultStatus::invalidParameter;
}


ResultStatus Ran(CommandCall<LongApp> call) {
	call.out.Write("ran");
	call.out.Line();
	return ResultStatus::ok;
}


using LongCommandSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("count", "count ticks", &LongCount, Argument<uint8>("times"))
		.Command("fail", "fails after a step", &LongFail)
		.Command("quick", "returns at once", &Ran);
}>;


using LongConsole = CommandLine<LongCommandSet, LongApp, LongCommands>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


int main() {
	// The first step runs at the Enter. Each further pass runs one step, and the pass after the last tick finishes with the prompt.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 2\r");

		CommandExpect(out.Text() == "count 2\r\ntick 1\r\n");

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ntick 2\r\n");

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ntick 2\r\n> ");
		CommandExpect(app.cancels == 0);
	}

	// A second run starts with a new state: the count starts again at one.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 1\r");
		console.Process();
		CommandExpect(out.Text() == "count 1\r\ntick 1\r\n> ");

		out.Clear();
		Type(console, "count 1\r");
		console.Process();
		CommandExpect(out.Text() == "count 1\r\ntick 1\r\n> ");
	}

	// Input typed while the command runs is not echoed or read until the command ends. Then it is read as usual.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 2\r");

		console.Feed('n');
		console.Feed('o');
		console.Feed('p');
		console.Feed('e');
		console.Feed('\r');

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ntick 2\r\n");

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ntick 2\r\n> nope\r\nunknown command 'nope', try help\r\n> ");
	}

	// Ctrl+C cancels the command: its handler gets one cancel call, and the Interrupt event follows with a new prompt.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 5\r");

		console.Feed(0x03);
		console.Process();

		CommandExpect(out.Text() == "count 5\r\ntick 1\r\n^C\r\n> ");
		CommandExpect(app.cancels == 1);

		out.Clear();
		Type(console, "quick\r");
		CommandExpect(out.Text() == "quick\r\nran\r\n> ");
		CommandExpect(app.cancels == 1);
	}

	// The bytes typed before a Ctrl+C that waits behind them are dropped with the command. The bytes after it are read.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 5\r");

		console.Feed('x');
		console.Feed('y');
		console.Feed(0x03);
		console.Feed('z');
		console.Process();

		CommandExpect(out.Text() == "count 5\r\ntick 1\r\n^C\r\n> z");
		CommandExpect(app.cancels == 1);

		console.Feed('\r');
		console.Process();
		CommandExpect(out.Text() == "count 5\r\ntick 1\r\n^C\r\n> z\r\nunknown command 'z', try help\r\n> ");
	}

	// A command that ends with a status other than ok raises CommandFailed with the command's name.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "fail\r");
		CommandExpect(out.Text() == "fail\r\n");

		console.Process();
		CommandExpect(out.Text() == "fail\r\nfail: invalidParameter\r\n> ");
	}

	// Input past the ring's size is lost while the command runs, and is reported before the next step.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		Type(console, "count 2\r");

		for (int index = 0; index < 33; index++) {
			console.Feed('a');
		}

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ninput lost\r\ntick 2\r\n");

		console.Process();
		CommandExpect(out.Text() == "count 2\r\ntick 1\r\ninput lost\r\ntick 2\r\n> " + std::string(32, 'a'));
	}

	// Closing the terminal cancels a running command, without an Interrupt event.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		LongConsole console(app, out);
		console.SetTerminalOpen(true);
		console.Process();
		CommandExpect(out.Text() == "VHAL command line\r\n> ");

		Type(console, "count 3\r");
		CommandExpect(out.Text() == "VHAL command line\r\n> count 3\r\ntick 1\r\n");

		console.SetTerminalOpen(false);
		console.Process();
		CommandExpect(out.Text() == "VHAL command line\r\n> count 3\r\ntick 1\r\n");
		CommandExpect(app.cancels == 1);
	}

	// A Ctrl+C that the full ring refuses still cancels the command at once. The command does not reach the place of the Ctrl+C while it
	// runs, so the cancel comes from the pending refusal. The lost byte is reported first, then the cancel.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		CommandLine<LongCommandSet, LongApp, LongCommands, InputRing<4>> console(app, out);
		Type(console, "count 5\r");

		console.Feed('a');
		console.Feed('b');
		console.Feed('c');
		console.Feed('d');
		console.Feed(0x03);

		console.Process();
		CommandExpect(out.Text() == "count 5\r\ntick 1\r\ninput lost\r\n^C\r\n> ");
		CommandExpect(app.cancels == 1);

		console.Process();
		CommandExpect(out.Text() == "count 5\r\ntick 1\r\ninput lost\r\n^C\r\n> ");
		CommandExpect(app.cancels == 1);
	}

	// An accepted Ctrl+C before a refused one cancels the command at its own place, and the refused press keeps its place and its own
	// Interrupt. Two presses give two Interrupts: the bytes between them are typed after the first cancel, and the second press cancels them.
	{
		CommandSim::RecordingPrint out;
		LongApp app;
		CommandLine<LongCommandSet, LongApp, LongCommands, InputRing<4>> console(app, out);
		Type(console, "count 5\r");

		console.Feed('a');
		console.Feed(0x03);
		console.Feed('b');
		console.Feed('c');
		console.Feed('d');
		console.Feed(0x03);

		console.Process();
		CommandExpect(out.Text() == "count 5\r\ntick 1\r\ninput lost\r\n^C\r\n> bc^C\r\n> ");
		CommandExpect(app.cancels == 1);
	}

	return CommandSim::Finish("TestCommandLongCommands");
}
