#include "CommandSim.h"
#include <Utilities/CommandLine/CommandHelp.h>
#include <Utilities/CommandLine/CommandHiddenInput.h>
#include <Utilities/CommandLine/CommandHistory.h>
#include <Utilities/CommandLine/CommandLine.h>
#include <Utilities/CommandLine/CommandLineEditing.h>
#include <cstddef>
#include <string>


// The HiddenInput module, fed one byte at a time. A Secret argument is echoed as '*' for each character, the quotes and the spaces inside
// its quotes too. Only the space that ends an argument is echoed as a space, and so is any character of a plain argument. The expected
// texts are written from the rules in CommandHiddenInput.h. The set has no Default(), so the standard texts are printed.


struct SecretApp {
	std::string user;
	std::string password;
	std::string word;
	int logins = 0;
};


ResultStatus Login(CommandCall<SecretApp> call, String<8> user, Secret<8> password) {
	call.app.user = std::string(user.View());
	call.app.password = std::string(password.View());
	call.app.logins++;
	return ResultStatus::ok;
}


ResultStatus Say(CommandCall<SecretApp> call, String<8> word) {
	call.app.word = std::string(word.View());
	return ResultStatus::ok;
}


using SecretSet = CommandSet<+[] {
	return CommandSetBuilder()
		.Command("login", "log in", &Login, Argument<String<8>>("user"), Argument<Secret<8>>("password"))
		.Command("say", "say a word", &Say, Argument<String<8>>("word"));
}>;


template<typename Console>
void Type(Console& console, const std::string& text) {
	for (char character : text) {
		console.Feed(static_cast<uint8>(character));
		console.Process();
	}
}


int main() {
	// The characters of the secret are echoed as '*'. The plain argument before it is echoed as typed.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob pw\r");

		CommandExpect(out.Text() == "login bob **\r\n> ");
		CommandExpect(app.user == "bob");
		CommandExpect(app.password == "pw");
		CommandExpect(app.logins == 1);
	}

	// Backspace erases one '*', as it erases one character.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob pwx\x08\r");

		CommandExpect(out.Text() == "login bob ***\b \b\r\n> ");
		CommandExpect(app.password == "pw");
	}

	// Inside the quotes of a secret every character is echoed as '*': both quotes and the space too, so the space does not show.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob \"a b\"\r");

		CommandExpect(out.Text() == "login bob *****\r\n> ");
		CommandExpect(app.password == "a b");
	}

	// The text is 7 characters with an escaped quote inside, and all 7 are hidden. The space after the closing quote ends the argument, so it
	// is echoed as a space. The word after it is past the last argument, and it is hidden too, so each of its characters is echoed as '*'.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob \"a \\\"b\" say\r");

		CommandExpect(out.Text() == "login bob " + std::string(7, '*') + " " + std::string(3, '*') + "\r\nlogin: too many arguments\r\n> ");
		CommandExpect(app.logins == 0);
	}

	// A space typed in a password without quotes ends the argument, so the rest of the password comes past the last argument. That rest is
	// hidden as well: the echo shows only the spaces that separate the words.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob pass word\r");

		CommandExpect(out.Text() == "login bob **** ****\r\nlogin: too many arguments\r\n> ");
		CommandExpect(app.logins == 0);
	}

	// Text after a quote-less space that ends a secret is hidden, and the space itself is echoed.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob ab\"cd ef\r");

		CommandExpect(out.Text() == "login bob ***** **\r\nlogin: too many arguments\r\n> ");
		CommandExpect(app.logins == 0);
	}

	// A command with no Secret argument shows its extra words as typed, because nothing in the line is a secret.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "say hi there\r");

		CommandExpect(out.Text() == "say hi there\r\nsay: too many arguments\r\n> ");
		CommandExpect(app.word.empty());
	}

	// A secret past its capacity is still echoed as '*', and the error names the argument, not the text.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "login bob 123456789\r");

		CommandExpect(out.Text() == "login bob *********\r\nlogin: password must be at most 8 characters\r\n> ");
		CommandExpect(app.logins == 0);
	}

	// A line that holds a secret is not kept by History, so Up does not bring it back. The line before it is kept.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, LineEditing<32>, History<4, 128>, HiddenInput> console(app, out);
		Type(console, "say hi\r");
		Type(console, "login bob pw\r");
		CommandExpect(out.Text() == "say hi\r\n> login bob **\r\n> ");

		Type(console, "\x1b[A");
		CommandExpect(out.Text() == "say hi\r\n> login bob **\r\n> say hi\x1b[K");

		Type(console, "\x1b[A");
		CommandExpect(out.Text() == "say hi\r\n> login bob **\r\n> say hi\x1b[K");
	}

	// A cursor move that brings a secret back to the screen does not print it: the tail of a line with a secret is drawn as '*'.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, LineEditing<32>, HiddenInput> console(app, out);
		Type(console, "login bob p");
		CommandExpect(out.Text() == "login bob *");

		Type(console, "\x1b[D");
		CommandExpect(out.Text() == "login bob *\x1b[1D");

		Type(console, "x");
		CommandExpect(out.Text() == "login bob *\x1b[1D**\x1b[K\x1b[1D");

		Type(console, "\r");
		CommandExpect(out.Text() == "login bob *\x1b[1D**\x1b[K\x1b[1D\r\n> ");
		CommandExpect(app.user == "bob");
		CommandExpect(app.password == "xp");
	}

	// A name that matches no command stops the parse. The rest of the line is then hidden too, so a password typed after a misspelt name is
	// not echoed. The spaces are hidden as well, so the length of the words does not show. The message still names the unknown word.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, HiddenInput> console(app, out);
		Type(console, "lgoin bob hunter2\r");

		CommandExpect(out.Text() == "lgoin " + std::string(11, '*') + "\r\nunknown command 'lgoin', try help\r\n> ");
		CommandExpect(app.logins == 0);
	}

	// The line with the misspelt name holds a secret, so History does not keep it: Up brings nothing back.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, LineEditing<32>, History<4, 128>, HiddenInput> console(app, out);
		Type(console, "lgoin bob hunter2\r" "\x1b[A\r");

		CommandExpect(out.Text() == "lgoin " + std::string(11, '*') + "\r\nunknown command 'lgoin', try help\r\n> " "\r\n> ");
	}

	// Help prints a Secret argument with its kind, and without a default.
	{
		CommandSim::RecordingPrint out;
		SecretApp app;
		CommandLine<SecretSet, SecretApp, Help, HiddenInput> console(app, out);
		Type(console, "help login\r");

		CommandExpect(out.Text() ==
			"help login\r\n"
			"login: log in\r\n"
			"usage: login <user> <password>\r\n"
			"  user      text of at most 8 characters\r\n"
			"  password  secret text of at most 8 characters, shown as *\r\n"
			"> ");
	}

	return CommandSim::Finish("TestCommandHiddenInput");
}
