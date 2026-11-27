#include "CommandSim.h"
#include <Utilities/CommandLine/CommandLine.h>
#include <cstddef>
#include <string>


// The module mechanism, with dummy modules. Each hook writes a marker to the console's output, so the test reads which hooks ran and
// what they consumed. A module that needs a feature is only accepted when a listed module provides it.


struct CountFeature {
};


struct ChainFeature {
};


// Provides CountFeature. Its hooks mark the output: a '!' is consumed, Up is consumed, a line end and a prompt are marked, and an
// UnknownCommand event is consumed.
struct CountingModule {
	using Provides = CountFeature;


	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		if (character == '!') {
			console.Output().Write("<!>");
			return true;
		}

		return false;
	}


	template<typename Console>
	bool OnKey(Console& console, CommandKey key) {
		if (key == CommandKey::Up) {
			console.Output().Write("<up>");
			return true;
		}

		return false;
	}


	template<typename Console>
	void OnLineEnd(Console& console) {
		console.Output().Write("[end]");
	}


	template<typename Console>
	bool OnEvent(Console& console, const CommandEvent& event) {
		if (event.kind == CommandEventKind::UnknownCommand) {
			console.Output().Write("(unknown)");
			return true;
		}

		return false;
	}


	template<typename Console>
	void OnPrompt(Console& console) {
		console.Output().Write("$");
	}
};


// Provides ChainFeature and needs CountFeature.
struct ChainModule {
	using Provides = ChainFeature;
	using Needs = CountFeature;


	template<typename Console>
	bool OnCharacter(Console& console, char character) {
		if (character == '#') {
			console.Output().Write("<#>");
			return true;
		}

		return false;
	}
};


struct ModuleApp {
	int keyCode = -1;
};


ResultStatus ModuleKey(CommandCall<ModuleApp> call, uint8 code) {
	call.app.keyCode = code;
	return ResultStatus::ok;
}


void ModuleEventHandler(CommandCall<ModuleApp> call, const CommandEvent& event) {
	static_cast<void>(call);
	static_cast<void>(event);
}


using ModuleCommands = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ModuleKey, Argument<uint8>("code"));
}>;


using ModuleCommandsWithDefault = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("key", "press a key", &ModuleKey, Argument<uint8>("code"))
		.Default(&ModuleEventHandler);
}>;


// The needs of a module are checked against all the listed modules, in any order.
static_assert(CommandModuleReady<CountingModule>());
static_assert(CommandModuleReady<ChainModule, CountingModule, ChainModule>());
static_assert(CommandModuleReady<ChainModule, ChainModule, CountingModule>());
static_assert(!CommandModuleReady<ChainModule, ChainModule>());
static_assert(!CommandModuleReady<ChainModule>());
static_assert(CommandProvided<CountFeature, CountingModule>());
static_assert(!CommandProvided<ChainFeature, CountingModule>());


int main() {
	// The hooks run in the order of the console: the character hook before the parser, the key hook for an escape sequence, the line end
	// hook at Enter, the event hook before the default, and the prompt hook before the prompt.
	{
		CommandSim::RecordingPrint out;
		ModuleApp app;
		CommandLine<ModuleCommands, ModuleApp, CountingModule> console(app, out);

		for (char character : std::string("a!b\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "a<!>b\r\n[end](unknown)$> ");
		CommandExpect(app.keyCode == -1);
		out.Clear();

		std::string up = std::string("\x1b") + "[A";
		for (char character : up + "\r") {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "<up>\r\n[end]$> ");
		out.Clear();

		// A key the module does not take is read as before.
		for (char character : std::string("key 9\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "key 9\r\n[end]$> ");
		CommandExpect(app.keyCode == 9);
		out.Clear();

		// The terminal greeting is an event the module does not take, so the standard greeting still prints, with the module's prompt.
		console.SetTerminalOpen(true);
		console.Process();
		CommandExpect(out.Text() == "VHAL command line\r\n$> ");
	}

	// A module that takes the UnknownCommand event keeps the standard text from being printed, and the default handler is not called for it.
	{
		CommandSim::RecordingPrint out;
		ModuleApp app;
		CommandLine<ModuleCommands, ModuleApp, CountingModule> console(app, out);
		for (char character : std::string("xyz\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "xyz\r\n[end](unknown)$> ");
	}

	// With no module listed, the same input prints the standard texts and no markers.
	{
		CommandSim::RecordingPrint out;
		ModuleApp app;
		CommandLine<ModuleCommands, ModuleApp> console(app, out);
		for (char character : std::string("a!b\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "a!b\r\nunknown command 'a!b', try help\r\n> ");
	}

	// A module with a need, listed with the module that provides it, runs as the others do.
	{
		CommandSim::RecordingPrint out;
		ModuleApp app;
		CommandLine<ModuleCommands, ModuleApp, ChainModule, CountingModule> console(app, out);
		for (char character : std::string("#\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "<#>\r\n[end]$> ");
	}

	// The set's Default() takes an event the module does not take: the missing argument goes to it, and prints nothing.
	{
		CommandSim::RecordingPrint out;
		ModuleApp app;
		CommandLine<ModuleCommandsWithDefault, ModuleApp, CountingModule> console(app, out);
		for (char character : std::string("key\r")) {
			console.Feed(static_cast<uint8>(character));
			console.Process();
		}

		CommandExpect(out.Text() == "key\r\n[end]$> ");
	}

	return CommandSim::Finish("TestCommandModules");
}
