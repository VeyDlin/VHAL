#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLineEditing.h>
#include <cstddef>
#include <string>


// The line editing module, fed one byte at a time. The expected texts are the terminal output written from the rules in
// CommandLineEditing.h: a move is CSI n D or CSI n C, the rest of the line is cleared with CSI K, a refused character rings a bell,
// and Backspace at the end echoes "\b \b". SimCommands has a default handler, which takes every error and prints nothing, so its
// defaults counter and kind show what the line was. NoDefaultCommands has none, so the standard texts are printed.


using NoDefaultCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &SimKey, Argument<uint8>("code"));
}>;


// The application of the default-only tests: the events it takes, in order, one line each.
struct WordLog {
	std::string events;
};


void LogWordEvent(CommandCall<WordLog> call, const CommandEvent& event) {
	if (event.kind == CommandEventKind::Word) {
		call.app.events += "Word:" + std::string(event.text) + "\n";
	} else if (event.kind == CommandEventKind::EndOfLine) {
		call.app.events += "EndOfLine\n";
	}
}


using WordOnly = CommandSet<+[] {
	return CommandSetBuilder().Default(&LogWordEvent);
}>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


// The top level of SimCommands, as the Help test writes it.
const std::string TopLevel =
	"  key    press a key\r\n"
	"  move   move the pointer\r\n"
	"  mode   set a mode\r\n"
	"  fail   always fails\r\n"
	"  a_b-9  a name with symbols\r\n"
	"  motor  (group) motor control\r\n";


int main() {
	// Typing at the end is echoed and parsed, as it is without the module.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\r");

		CommandExpect(out.Text() == "key 7\r\n> ");
		CommandExpect(app.keyCode == 7);
		CommandExpect(app.defaults == 0);
	}

	// Left, then a digit: the digit goes in before the 7, the 7 moves right and the cursor comes back after the digit.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[D" "1\r");

		CommandExpect(out.Text() == "key 7\x1b[1D" "17\x1b[K\x1b[1D\r\n> ");
		CommandExpect(app.keyCode == 17);
	}

	// Backspace in the middle removes the character before the cursor and redraws the tail.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 17\x1b[D" "\x08\r");

		CommandExpect(out.Text() == "key 17\x1b[1D\x1b[1D" "7\x1b[K\x1b[1D\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// DEL at the end erases the last character, as Backspace does, and the echo is the same.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 17\x7f\r");

		CommandExpect(out.Text() == "key 17\b \b\r\n> ");
		CommandExpect(app.keyCode == 1);
	}

	// Delete removes the character at the cursor, which is the 7 after the Left key. Nothing follows it, so the screen only needs the erase.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 17\x1b[D" "\x1b[3~\r");

		CommandExpect(out.Text() == "key 17\x1b[1D\x1b[K\r\n> ");
		CommandExpect(app.keyCode == 1);
	}

	// Home and End: CSI H and CSI F, or CSI 1 ~ and CSI 4 ~. The cursor moves across the five characters.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[H\x1b[F\r");

		CommandExpect(out.Text() == "key 7\x1b[5D\x1b[5C\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[1~\x1b[4~\r");

		CommandExpect(out.Text() == "key 7\x1b[5D\x1b[5C\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// Ctrl+A and Ctrl+E do the same as Home and End.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x01\x05\r");

		CommandExpect(out.Text() == "key 7\x1b[5D\x1b[5C\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// Right moves back over the character Left passed. At the ends of the line, the edits that need a character do nothing: Backspace
	// and Ctrl+W at the start, and Delete and Ctrl+K at the end.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 17\x1b[D" "\x1b[C\r");

		CommandExpect(out.Text() == "key 17\x1b[1D\x1b[1C\r\n> ");
		CommandExpect(app.keyCode == 17);
	}

	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[H\x08\x17\r");

		CommandExpect(out.Text() == "key 7\x1b[5D\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[3~\x0b\r");

		CommandExpect(out.Text() == "key 7\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// A character typed at the start goes before the first character, and the line is parsed as it stands.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\x1b[Hx\r");

		CommandExpect(out.Text() == "key 7\x1b[5Dxkey 7\x1b[K\x1b[5D\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::UnknownCommand));
		CommandExpect(app.defaultText == "xkey");
	}

	// Ctrl+K from the cursor after "key " leaves "key ", which has no code: the argument is missing.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 17\x1b[D" "\x1b[D" "\x0b\r");

		CommandExpect(out.Text() == "key 17\x1b[1D\x1b[1D" "\x1b[K\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::ArgumentError));
	}

	// Ctrl+W at the end erases the word before the cursor, one character at a time through the parser.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 12\x17\r");

		CommandExpect(out.Text() == "key 12\b \b\b \b\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::ArgumentError));
	}

	// Ctrl+W in the middle removes "key" from "key 12", which leaves " 12": the word is unknown at Enter.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 12\x1b[D" "\x1b[D" "\x1b[D" "\x17\r");

		CommandExpect(out.Text() == "key 12\x1b[1D\x1b[1D\x1b[1D" "\x1b[3D 12\x1b[K\x1b[3D\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::UnknownCommand));
		CommandExpect(app.defaultText == "12");
	}

	// A buffer of 8 bytes takes "key 1234" and refuses the next character with a bell. The refused character is neither echoed nor
	// parsed. The number is out of range for the code, so the line ends with an argument error.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<8>> console(app, out);
		Type(console, "key 1234" "5\r");

		CommandExpect(out.Text() == "key 1234\x07\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::ArgumentError));
	}

	// A Tab that no module takes is typed as a space, through the modules, so the editor keeps it.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key\t7\r");

		CommandExpect(out.Text() == "key 7\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// Ctrl+C cancels the line and clears the buffer. The Left key after it moves nothing, because the cursor is at the start.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<NoDefaultCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7" "\x03" "\x1b[D" "9\r");

		CommandExpect(out.Text() == "key 7^C\r\n> 9\r\nunknown command '9', try help\r\n> ");
	}

	// After a line end the buffer is cleared at the prompt: the Left key after "key 7" moves nothing.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<NoDefaultCommands, SimApp, LineEditing<32>> console(app, out);
		Type(console, "key 7\r" "\x1b[D" "9\r");

		CommandExpect(out.Text() == "key 7\r\n> 9\r\nunknown command '9', try help\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// Help reads the whole line from the buffer: "hep" with an l inserted before the p is "help", so Help answers it.
	// Help listed before LineEditing and after it give the same screen.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help, LineEditing<32>> console(app, out);
		Type(console, "hep\x1b[D" "l\r");

		CommandExpect(out.Text() == "hep\x1b[1D" "lp\x1b[K\x1b[1D" "\r\n" + TopLevel + "> ");
		CommandExpect(app.defaults == 0);
	}

	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Help> console(app, out);
		Type(console, "hep\x1b[D" "l\r");

		CommandExpect(out.Text() == "hep\x1b[1D" "lp\x1b[K\x1b[1D" "\r\n" + TopLevel + "> ");
		CommandExpect(app.defaults == 0);
	}

	// A default-only build with LineEditing raises no Word event while the line is typed. At Enter, the words of the edited line are raised
	// from the buffer, then EndOfLine. "ab cd" with Home and an x at the start is the line "xab cd", so the words are "xab" and "cd".
	{
		CommandSim::RecordingPrint out;
		WordLog log;
		CommandLine<WordOnly, WordLog, LineEditing<32>> console(log, out);
		Type(console, "ab cd");

		CommandExpect(log.events.empty());

		Type(console, "\x01" "x\r");

		CommandExpect(out.Text() == "ab cd\x1b[5Dxab cd\x1b[K\x1b[5D\r\n> ");
		CommandExpect(log.events == "Word:xab\nWord:cd\nEndOfLine\n");
	}

	// Backspace at the end can go back over the space into the first word, because the words are read at Enter. "ab cd" with four
	// Backspaces is "a", which is the only word at Enter.
	{
		CommandSim::RecordingPrint out;
		WordLog log;
		CommandLine<WordOnly, WordLog, LineEditing<32>> console(log, out);
		Type(console, "ab cd\x08\x08\x08\x08\r");

		CommandExpect(out.Text() == "ab cd\b \b\b \b\b \b\b \b\r\n> ");
		CommandExpect(log.events == "Word:a\nEndOfLine\n");
	}

	// A Backspace that leaves a word still ends at its space: "ab cd" with three Backspaces is "ab", and Enter raises Word "ab".
	{
		CommandSim::RecordingPrint out;
		WordLog log;
		CommandLine<WordOnly, WordLog, LineEditing<32>> console(log, out);
		Type(console, "ab cd\x08\x08\x08\r");

		CommandExpect(out.Text() == "ab cd\b \b\b \b\b \b\r\n> ");
		CommandExpect(log.events == "Word:ab\nEndOfLine\n");
	}

	return CommandSim::Finish("TestCommandLineEditing");
}
