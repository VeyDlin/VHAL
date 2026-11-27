#include "CommandSim.h"
#include "CommandSimSets.h"
#include <Utilities/CommandLine/CommandHistory.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLineEditing.h>
#include <cstddef>
#include <string>


// The history module, fed one byte at a time. The expected texts are the terminal output: a recalled line is written from the start of
// the line (CSI n D back over the cursor), then the line is cleared to its end (CSI K). SimCommands has a default handler, so a
// command that runs prints nothing, and app.keyCode shows which "key" line ran last.


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


int main() {
	// Up with no line before it does nothing and takes the key.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "\x1b[A" "\r");

		CommandExpect(out.Text() == "\r\n> ");
	}

	// Up recalls the newest line into the buffer, where it is replayed, and Enter runs it. The line is not kept a second time.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\rkey 2\r" "\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> key 2\r\n> " "key 2\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 2);
	}

	// Up three times reaches the oldest line and stops there. Enter runs that line.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\rkey 2\r" "\x1b[A\x1b[A\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> key 2\r\n> " "key 2\x1b[K" "\x1b[5D" "key 1\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 1);
	}

	// "ke" is typed, then Up recalls "key 2", then Down brings "ke" back. The draft is not kept after Down, and "y 9" completes it.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\rkey 2\rke" "\x1b[A" "\x1b[B" "y 9\r");

		CommandExpect(out.Text() == "key 1\r\n> key 2\r\n> ke" "\x1b[2D" "key 2\x1b[K" "\x1b[5D" "ke\x1b[K" "y 9" "\r\n> ");
		CommandExpect(app.keyCode == 9);
	}

	// Empty lines are not kept, and a line that repeats the newest one is not kept either. The ring then holds one line: "key 1".
	// Two Ups show it, and a third has nothing above it.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\r" "\r" "key 1\r" "\x1b[A\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> " "\r\n> " "key 1\r\n> " "key 1\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 1);
	}

	// Three lines are kept, so the fourth line drops the first. Up four times shows the three that remain, and then stops.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\rkey 2\rkey 3\rkey 4\r" "\x1b[A\x1b[A\x1b[A\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> key 2\r\n> key 3\r\n> key 4\r\n> " "key 4\x1b[K" "\x1b[5D" "key 3\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 3);
	}

	// A ring of 20 bytes holds two lines of 7 bytes, and the third wraps around the end of the ring. Up reaches the two newest lines.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<8, 20>> console(app, out);
		Type(console, "key 1\rkey 2\rkey 3\rkey 4\r" "\x1b[A\x1b[A\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> key 2\r\n> key 3\r\n> key 4\r\n> " "key 4\x1b[K" "\x1b[5D" "key 3\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 3);
	}

	// A ring of 8 bytes holds one line of 3 characters. A draft of 5 characters cannot be kept beside it, so Up does nothing and the line
	// is not lost: after Ctrl+C the empty line recalls it.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<2, 8>> console(app, out);
		Type(console, "abc\rxyzwv\x1b[A\x1b[A\x03\x1b[A\r");

		CommandExpect(out.Text() == "abc\r\n> " "xyzwv" "> " "abc\x1b[K" "\r\n> ");
	}

	// Ctrl+C during a recall drops the draft with the line, so the next Up keeps the new line and shows the newest older one.
	{
		CommandSim::RecordingPrint out;
		SimApp app;
		CommandLine<SimCommands, SimApp, LineEditing<32>, History<3, 64>> console(app, out);
		Type(console, "key 1\r" "\x1b[A" "\x03" "ab" "\x1b[A" "\r");

		CommandExpect(out.Text() == "key 1\r\n> " "key 1\x1b[K" "> " "ab" "\x1b[2D" "key 1\x1b[K" "\r\n> ");
		CommandExpect(app.keyCode == 1);
	}

	return CommandSim::Finish("TestCommandHistory");
}
