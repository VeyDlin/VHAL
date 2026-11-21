#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <cstddef>
#include <string>


// The Help module. The expected texts are written from the layout rules in CommandHelp.h: names padded to the longest name of a level
// plus two spaces, groups marked (group), a usage line with the path, and one line per argument.


using NamedHelpCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("help", "a command named help", &SimKey, Argument<uint8>("code"));
}>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


// The top level of SimCommands, after its echo of Enter.
const std::string TopLevel =
	"  key    press a key\r\n"
	"  move   move the pointer\r\n"
	"  mode   set a mode\r\n"
	"  fail   always fails\r\n"
	"  a_b-9  a name with symbols\r\n"
	"  motor  (group) motor control\r\n";


const std::string MotorLevel =
	"motor: motor control\r\n"
	"  speed  set the speed\r\n"
	"  stop   stop the motor\r\n"
	"  pid    (group) pid tuning\r\n";


int main() {
	// help lists the top level, and the line is taken: no default handler call, and the prompt follows.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);
		Type(console, "help\r");

		CommandExpect(out.Text() == "help\r\n" + TopLevel + "> ");
		CommandExpect(app.defaults == 0);
	}

	// The same listing with a tab, which the console echoes as a space. A tab is a word break, as a space is.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);
		Type(console, "help\t\r");

		CommandExpect(out.Text() == "help \r\n" + TopLevel + "> ");
	}

	// help with a group path lists the group, with its help text first.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);
		Type(console, "help motor\r");
		CommandExpect(out.Text() == "help motor\r\n" + MotorLevel + "> ");
		out.Clear();

		Type(console, "help motor pid\r");
		CommandExpect(out.Text() == "help motor pid\r\npid: pid tuning\r\n  gain  set the gain\r\n> ");
		out.Clear();

		// A path that ends at a command prints the command.
		Type(console, "help motor pid gain\r");
		CommandExpect(out.Text() == "help motor pid gain\r\ngain: set the gain\r\nusage: motor pid gain <value>\r\n  value  number -1000..1000\r\n> ");
		out.Clear();

		Type(console, "motor -h\r");
		CommandExpect(out.Text() == "motor -h\r\n" + MotorLevel + "> ");
		CommandExpect(app.defaults == 0);
	}

	// help with a command name prints its usage, with the range or values and the defaults of its arguments.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		Type(console, "help key\r");
		CommandExpect(out.Text() == "help key\r\nkey: press a key\r\nusage: key <code>\r\n  code  number 0..255\r\n> ");
		out.Clear();

		Type(console, "help move\r");
		CommandExpect(out.Text() ==
			"help move\r\n"
			"move: move the pointer\r\n"
			"usage: move <x> <y> [fast]\r\n"
			"  x     number -127..127\r\n"
			"  y     number -127..127\r\n"
			"  fast  on or off, default off\r\n"
			"> ");
		out.Clear();

		Type(console, "help mode\r");
		CommandExpect(out.Text() ==
			"help mode\r\n"
			"mode: set a mode\r\n"
			"usage: mode <mode> [label]\r\n"
			"  mode   one of fast, slow\r\n"
			"  label  text of at most 8 characters, default \"none\"\r\n"
			"> ");
		out.Clear();

		Type(console, "help fail\r");
		CommandExpect(out.Text() == "help fail\r\nfail: always fails\r\nusage: fail\r\n> ");
		out.Clear();

		Type(console, "help a_b-9\r");
		CommandExpect(out.Text() == "help a_b-9\r\na_b-9: a name with symbols\r\nusage: a_b-9 <code>\r\n  code  number 0..255\r\n> ");
		out.Clear();

		// An optional integer shows its default, and a path with a group shows the group's name in the usage.
		Type(console, "help motor speed\r");
		CommandExpect(out.Text() == "help motor speed\r\nspeed: set the speed\r\nusage: motor speed [rpm]\r\n  rpm  number -1000..1000, default 0\r\n> ");
		out.Clear();

		Type(console, "help motor stop\r");
		CommandExpect(out.Text() == "help motor stop\r\nstop: stop the motor\r\nusage: motor stop\r\n> ");
		out.Clear();

		// Words after the command are ignored.
		Type(console, "help key 9\r");
		CommandExpect(out.Text() == "help key 9\r\nkey: press a key\r\nusage: key <code>\r\n  code  number 0..255\r\n> ");
		out.Clear();

		// key -h is the same request as help key, and the key handler does not run.
		Type(console, "key -h\r");
		CommandExpect(out.Text() == "key -h\r\nkey: press a key\r\nusage: key <code>\r\n  code  number 0..255\r\n> ");
		CommandExpect(app.keyCode == -1);
		CommandExpect(app.defaults == 0);
		out.Clear();

		// A path with a command in the middle: the command's help, not the level's.
		Type(console, "mode fast -h\r");
		CommandExpect(out.Text() ==
			"mode fast -h\r\n"
			"mode: set a mode\r\n"
			"usage: mode <mode> [label]\r\n"
			"  mode   one of fast, slow\r\n"
			"  label  text of at most 8 characters, default \"none\"\r\n"
			"> ");
	}

	// A trailing -h is a request even after help, so help motor -h answers as motor -h does. help -h has no path, so it lists the top level.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		Type(console, "help motor -h\r");
		CommandExpect(out.Text() == "help motor -h\r\n" + MotorLevel + "> ");
		out.Clear();

		Type(console, "help -h\r");
		CommandExpect(out.Text() == "help -h\r\n" + TopLevel + "> ");
	}

	// A word that names nothing at its level is an unknown command, with the standard text.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		Type(console, "help nothing\r");
		CommandExpect(out.Text() == "help nothing\r\nunknown command 'nothing', try help\r\n> ");
		out.Clear();

		Type(console, "help motor xyz\r");
		CommandExpect(out.Text() == "help motor xyz\r\nunknown command 'xyz', try help\r\n> ");
		out.Clear();

		Type(console, "xyz -h\r");
		CommandExpect(out.Text() == "xyz -h\r\nunknown command 'xyz', try help\r\n> ");
		CommandExpect(app.defaults == 0);
	}

	// Without a help request, a line runs as before: help with no Help listed is an unknown command, which the default handler gets.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp> console(app, out);

		Type(console, "help\r");
		CommandExpect(out.Text() == "help\r\n> ");
		CommandExpect(app.defaults == 1);
		CommandExpect(app.defaultKind == static_cast<int>(CommandEventKind::UnknownCommand));
		CommandExpect(app.defaultText == "help");
	}

	// A command named help is not a help request: the word runs the command.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<NamedHelpCommands, SimApp, Help> console(app, out);

		Type(console, "help\r");
		CommandExpect(out.Text() == "help\r\nhelp: code is missing\r\n> ");
		out.Clear();

		Type(console, "help 7\r");
		CommandExpect(out.Text() == "help 7\r\n> ");
		CommandExpect(app.keyCode == 7);
	}

	// Backspace: the copy of the line follows the console's erasing, so a corrected word is the word that runs.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		Type(console, "helx");
		Type(console, std::string(1, static_cast<char>(0x7F)));
		Type(console, "p\r");
		CommandExpect(out.Text() == "helx\b \bp\r\n" + TopLevel + "> ");
		out.Clear();

		// A command that is typed, erased to nothing, and replaced by a help request: the line is the last one typed.
		Type(console, "key 3\b\b\b\b\bhelp\r");
		CommandExpect(out.Text() == "key 3\b \b\b \b\b \b\b \b\b \bhelp\r\n" + TopLevel + "> ");
		CommandExpect(app.keyCode == -1);
	}

	// Ctrl+C cancels the line, and the help request in it is gone with it.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		Type(console, "help\x03\r");
		CommandExpect(out.Text() == "help> \r\n> ");
		CommandExpect(app.interrupts == 1);
	}

	// A line longer than the copy of Help is not a help request. It runs as a line, so the unknown word goes to the default handler.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, Help> console(app, out);

		std::string spaces(66, ' ');
		Type(console, "help" + spaces + "\r");
		CommandExpect(out.Text() == "help" + spaces + "\r\n> ");
		CommandExpect(app.defaults == 1);
	}

	return CommandSim::Finish("TestCommandHelp");
}
