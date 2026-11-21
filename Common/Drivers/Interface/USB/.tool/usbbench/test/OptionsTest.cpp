#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "Options.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


// Parses a command line given as words, without the program name. Returns the parse result and the error text.
ParseResult ParseWords(const std::vector<std::string>& words, Options& options, std::string& error) {
	std::vector<std::string> storage;
	storage.push_back("usbbench");
	storage.insert(storage.end(), words.begin(), words.end());

	std::vector<char*> argv;
	for (std::string& word : storage) {
		argv.push_back(word.data());
	}

	return ParseOptions(static_cast<int>(argv.size()), argv.data(), options, error);
}


// Parses the words and checks the result. The error is only checked to be non-empty when a failure is expected.
void CheckParse(const char* what, const std::vector<std::string>& words, ParseResult expected) {
	Options options;
	std::string error;
	ParseResult result = ParseWords(words, options, error);
	Expect(result == expected, what);
	if (expected == ParseResult::Error) {
		Expect(!error.empty(), what);
	}
}


void CheckList() {
	Options options;
	std::string error;
	Expect(ParseWords({ "list" }, options, error) == ParseResult::Ok, "list parses");
	Expect(options.command == Command::List, "list selects the list command");
	Expect(options.serial.empty(), "list has no serial by default");
	Expect(!options.allDevices, "list has no --all by default");

	CheckParse("list takes no argument", { "list", "extra" }, ParseResult::Error);
	CheckParse("list refuses --serial", { "list", "--serial", "ABC" }, ParseResult::Error);
	CheckParse("list refuses --all", { "list", "--all" }, ParseResult::Error);
}


void CheckSerial() {
	Options options;
	std::string error;
	Expect(ParseWords({ "in", "64k" }, options, error) == ParseResult::Ok, "in without --serial parses");
	Expect(options.serial.empty(), "without --serial the serial is empty");
	Expect(!options.allDevices, "without --all the switch is off");

	Options spaced;
	Expect(ParseWords({ "in", "64k", "--serial", "0A1B2C" }, spaced, error) == ParseResult::Ok, "--serial with a separate value");
	Expect(spaced.serial == "0A1B2C", "--serial takes the value after it");

	Options joined;
	Expect(ParseWords({ "--serial=0A1B2C", "info" }, joined, error) == ParseResult::Ok, "--serial=value parses");
	Expect(joined.serial == "0A1B2C", "--serial=value takes the value");
	Expect(joined.command == Command::Info, "--serial before the command still selects it");

	CheckParse("--serial needs a value", { "info", "--serial" }, ParseResult::Error);
	CheckParse("--serial needs a non-empty value", { "in", "64k", "--serial", "" }, ParseResult::Error);
	CheckParse("--serial= needs a non-empty value", { "in", "64k", "--serial=" }, ParseResult::Error);

	// "--serial --all" must not take the switch as the serial number: the value is refused, and the switch is not applied.
	Options swallowed;
	ParseResult swallowedResult = ParseWords({ "in", "64k", "--serial", "--all" }, swallowed, error);
	Expect(swallowedResult == ParseResult::Error, "--serial followed by --all is refused");
	Expect(error.find("--serial needs a value") != std::string::npos, "the refusal says --serial needs a value");
	Expect(swallowed.serial.empty() && !swallowed.allDevices, "the refused --serial sets neither the serial nor --all");

	CheckParse("--serial=--all is refused", { "in", "64k", "--serial=--all" }, ParseResult::Error);
	CheckParse("--serial followed by another option is refused", { "info", "--serial", "--backend" }, ParseResult::Error);
}


void CheckAll() {
	Options options;
	std::string error;
	Expect(ParseWords({ "in", "64k", "--all" }, options, error) == ParseResult::Ok, "in --all parses");
	Expect(options.allDevices, "--all sets the switch");
	Expect(options.serial.empty(), "--all leaves the serial empty");

	Options first;
	Expect(ParseWords({ "--all", "out", "64k" }, first, error) == ParseResult::Ok, "--all before the command parses");
	Expect(first.allDevices && first.command == Command::Out, "--all before the command sets the switch");

	Options loopback;
	Expect(ParseWords({ "loopback", "64k", "--all" }, loopback, error) == ParseResult::Ok, "loopback --all parses");
	Expect(loopback.allDevices, "loopback --all sets the switch");

	Options sourceSink;
	Expect(ParseWords({ "sourcesink", "64k", "--all" }, sourceSink, error) == ParseResult::Ok, "sourcesink --all parses");
	Expect(sourceSink.allDevices, "sourcesink --all sets the switch");

	CheckParse("info refuses --all", { "info", "--all" }, ParseResult::Error);
	CheckParse("counters refuses --all", { "counters", "--all" }, ParseResult::Error);
	CheckParse("reconnect refuses --all", { "reconnect", "500", "--all" }, ParseResult::Error);
	CheckParse("--all and --serial together are refused", { "in", "64k", "--all", "--serial", "ABC" }, ParseResult::Error);
	CheckParse("--all does not take a value", { "in", "64k", "--all=1" }, ParseResult::Error);
}


void CheckExistingRulesStill() {
	CheckParse("in needs a byte count", { "in" }, ParseResult::Error);
	CheckParse("in byte count must be whole packets", { "in", "100" }, ParseResult::Error);
	CheckParse("unknown option is refused", { "in", "64k", "--nope" }, ParseResult::Error);
	CheckParse("help is recognised", { "--help" }, ParseResult::Help);
}


}


int main() {
	CheckList();
	CheckSerial();
	CheckAll();
	CheckExistingRulesStill();

	std::printf("usbbench options test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
