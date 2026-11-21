#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandCompletion.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLineEditing.h>
#include <cstddef>
#include <string>


// The completion module, fed one byte at a time. The expected texts are the terminal output. A choice list comes in declaration order:
// SimCommands declares key, move, mode, fail, a_b-9 and motor, and the values of SimMode are declared fast, then slow. SimCommands has a
// default handler, so an error that the completed line causes prints nothing. Each test names the modules it lists, since the module order
// is part of the test.


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


int main() {
	// "ke" and Tab: key is the only name that continues it, so the "y" is typed in. Enter then runs key, with no code.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "ke\t\r");

		CommandExpect(out.Text() == "key\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::ArgumentError));
	}

	// "m" and Tab: mode, move and motor share "mo", which is typed in. The second Tab lists the three, and writes the prompt and
	// the line again.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "m\t\t");

		CommandExpect(out.Text() == "mo\r\nmove  mode  motor\r\n> mo");
	}

	// No name starts with z, so Tab does nothing, twice.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "z\t\t");

		CommandExpect(out.Text() == "z");
	}

	// An empty line: the first Tab types nothing, and the second lists every command and group of the set.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "\t\t");

		CommandExpect(out.Text() == "\r\nkey  move  mode  fail  a_b-9  motor\r\n> ");
	}

	// The cursor is before the end of "ke": Tab moves it to the end first, and then types the "y".
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "ke\x1b[D\t\r");

		CommandExpect(out.Text() == "ke\x1b[1D\x1b[1Cy\r\n> ");
		CommandExpect(app.defaults == 1);
	}

	// After a Tab, a change to the line (here a Backspace) means the next Tab types the common part again. It does not list yet.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "m\t\x08\t\t");

		CommandExpect(out.Text() == "mo\b \bo\r\nmove  mode  motor\r\n> mo");
	}

	// An enum argument: after "mode " the values are listed on the second Tab, in the order they are declared: fast, then slow.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "mode \t\t");

		CommandExpect(out.Text() == "mode \r\nfast  slow\r\n> mode ");
	}

	// "s" is the only value that starts with s, so Tab types "low" and the line runs with mode slow.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "mode s\t\r");

		CommandExpect(out.Text() == "mode slow\r\n> ");
		CommandExpect(app.mode == SimMode::Slow);
	}

	// In a group: "motor p" completes to "pid", and Enter ends on the group, which is an unknown command for the default handler.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "motor p\t\r");

		CommandExpect(out.Text() == "motor pid\r\n> ");
		CommandExpect(app.defaults == 1);
	}

	// In the motor group, after "motor " the names of the group are listed in declaration order: speed, stop, then the pid group.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "motor \t\t");

		CommandExpect(out.Text() == "motor \r\nspeed  stop  pid\r\n> motor ");
	}

	// Completion comes before LineEditing: the same result.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Completion, LineEditing<32>> console(app, out);
		Type(console, "ke\t\r");

		CommandExpect(out.Text() == "key\r\n> ");
		CommandExpect(app.defaults == 1);
	}

	// Without LineEditing the completion still types, and the same Enter runs the line.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Completion> console(app, out);
		Type(console, "ke\t\r");

		CommandExpect(out.Text() == "key\r\n> ");
		CommandExpect(app.defaults == 1);
	}

	// Without LineEditing the console keeps no text, so a second Tab cannot list the choices and write the line again. The first Tab types
	// the shared "o", and each Tab after it with no change to the line rings the bell (0x07) and lists nothing.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Completion> console(app, out);
		Type(console, "m\t\t\t");

		CommandExpect(out.Text() == "mo\x07\x07");
	}

	// A Tab after the last argument of a command has nothing to complete. The Tab is taken, and nothing is typed.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, Completion> console(app, out);
		Type(console, "key 7 \t\r");

		CommandExpect(out.Text() == "key 7 \r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	return CommandSim::Finish("TestCommandCompletion");
}
