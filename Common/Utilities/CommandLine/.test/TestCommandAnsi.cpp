#include "CommandSim.h"
#include <Utilities/CommandLine/CommandAnsi.h>
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLineEditing.h>
#include <cstddef>
#include <string>


// The Ansi module, fed one byte at a time. The expected texts are the escape sequences written from the rules in CommandAnsi.h: a
// green prompt, red standard errors, and Ctrl+L clearing the screen and writing the prompt and the line again. A console without Ansi
// writes no escape sequence at all.


struct AnsiApp {
	int fails = 0;
	int defaults = 0;
};


ResultStatus AnsiFail(CommandCall<AnsiApp> call) {
	call.app.fails++;
	return ResultStatus::invalidParameter;
}


using AnsiSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("fail", "always fails", &AnsiFail);
}>;


void AnsiDefault(CommandCall<AnsiApp> call, const CommandEvent& event) {
	static_cast<void>(event);
	call.app.defaults++;
	call.out.Write("event");
	call.out.Line();
}


using DefaultAnsiSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("fail", "always fails", &AnsiFail)
		.Default(&AnsiDefault);
}>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


const std::string Green = "\x1b[32m> \x1b[0m";
const std::string Red = "\x1b[31m";
const std::string Reset = "\x1b[0m";


int main() {
	// The greeting and the prompt are green.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>, Ansi> console(app, out);
		console.SetTerminalOpen(true);
		console.Process();

		CommandExpect(out.Text() == "VHAL command line\r\n" + Green);
	}

	// An unknown command is printed in red, and the prompt follows it.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>, Ansi> console(app, out);
		Type(console, "nope\r");

		CommandExpect(out.Text() == "nope\r\n" + Red + "unknown command 'nope', try help\r\n" + Reset + Green);
	}

	// A command that fails is printed in red with its name and status.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>, Ansi> console(app, out);
		Type(console, "fail\r");

		CommandExpect(out.Text() == "fail\r\n" + Red + "fail: invalidParameter\r\n" + Reset + Green);
		CommandExpect(app.fails == 1);
	}

	// Ctrl+L clears the screen, writes the prompt again, then the line that was being typed.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>, Ansi> console(app, out);
		console.SetTerminalOpen(true);
		console.Process();
		Type(console, "ab");
		console.Feed(0x0C);
		console.Process();

		CommandExpect(out.Text() == "VHAL command line\r\n" + Green + "ab" + "\x1b[2J\x1b[H" + Green + "ab");

		Type(console, "\r");
		CommandExpect(out.Text() == "VHAL command line\r\n" + Green + "ab" + "\x1b[2J\x1b[H" + Green + "ab" + "\r\n" + Red + "unknown command 'ab', try help\r\n" + Reset + Green);
	}

	// Ctrl+L with the cursor in the middle of the line writes the line again and moves the cursor back to its place, so the next character
	// goes where the user put it: abc, Left, Ctrl+L, x gives the line abxc with the cursor after the x.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>, Ansi> console(app, out);
		Type(console, "abc\x1b[D");
		out.Clear();

		console.Feed(0x0C);
		console.Process();
		CommandExpect(out.Text() == "\x1b[2J\x1b[H" + Green + "abc\x1b[1D");

		out.Clear();
		Type(console, "x");
		CommandExpect(out.Text() == "xc\x1b[K\x1b[1D");

		Type(console, "\r");
		CommandExpect(out.Text() == "xc\x1b[K\x1b[1D\r\n" + Red + "unknown command 'abxc', try help\r\n" + Reset + Green);
	}

	// Without Ansi there is no escape sequence: the errors and the prompt are plain, and Ctrl+L is ignored.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<AnsiSet, AnsiApp, Help, LineEditing<32>> console(app, out);
		Type(console, "nope\r");
		CommandExpect(out.Text() == "nope\r\nunknown command 'nope', try help\r\n> ");

		out.Clear();
		console.Feed(0x0C);
		console.Process();
		CommandExpect(out.Text().empty());
	}

	// A set with its own Default() gets the errors itself: Ansi colours none of them, and it still colours the prompt.
	{
		CommandSim::RecordingPrint out;
		AnsiApp app;
		CommandLine<DefaultAnsiSet, AnsiApp, Ansi> console(app, out);
		Type(console, "fail\r");

		CommandExpect(out.Text() == "fail\r\nevent\r\n" + Green);
		CommandExpect(app.defaults == 1);
	}

	return CommandSim::Finish("TestCommandAnsi");
}
