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


// Parses a command line given as words, without the program name.
ParseResult ParseWords(const std::vector<std::string>& words, Options& options, std::string& error) {
	std::vector<std::string> storage;
	storage.push_back("usbclasscheck");
	storage.insert(storage.end(), words.begin(), words.end());

	std::vector<char*> argv;
	for (std::string& word : storage) {
		argv.push_back(word.data());
	}

	return ParseOptions(static_cast<int>(argv.size()), argv.data(), options, error);
}


// The parse must give the expected result. An error must come with a message.
void CheckParse(const char* what, const std::vector<std::string>& words, ParseResult expected) {
	Options options;
	std::string error;
	ParseResult result = ParseWords(words, options, error);
	Expect(result == expected, what);
	if (expected == ParseResult::Error) {
		Expect(!error.empty(), what);
	}
}


void CheckCommands() {
	Options options;
	std::string error;
	Expect(ParseWords({ "list" }, options, error) == ParseResult::Ok, "list parses");
	Expect(options.command == Command::List, "list selects the list command");

	Options composite;
	Expect(ParseWords({ "composite" }, composite, error) == ParseResult::Ok, "composite parses");
	Expect(composite.command == Command::Composite, "composite selects the composite command");
	Expect(composite.compositeCheck == CompositeCheck::All, "composite without a check runs all three");

	Options storage;
	Expect(ParseWords({ "storage" }, storage, error) == ParseResult::Ok, "storage parses");
	Expect(storage.command == Command::Storage, "storage selects the storage command");

	Options dfu;
	Expect(ParseWords({ "dfu" }, dfu, error) == ParseResult::Ok, "dfu parses");
	Expect(dfu.command == Command::Dfu, "dfu selects the dfu command");

	CheckParse("an unknown command is refused", { "flash" }, ParseResult::Error);
	CheckParse("no command is refused", { "--serial", "0A1B" }, ParseResult::Error);
	CheckParse("list takes no argument", { "list", "extra" }, ParseResult::Error);
	CheckParse("storage takes no argument", { "storage", "cdc" }, ParseResult::Error);
}


void CheckCompositeChecks() {
	Options cdc;
	std::string error;
	Expect(ParseWords({ "composite", "cdc" }, cdc, error) == ParseResult::Ok, "composite cdc parses");
	Expect(cdc.compositeCheck == CompositeCheck::Cdc, "composite cdc selects the cdc check");

	Options hid;
	Expect(ParseWords({ "composite", "hid" }, hid, error) == ParseResult::Ok, "composite hid parses");
	Expect(hid.compositeCheck == CompositeCheck::Hid, "composite hid selects the hid check");

	Options vendor;
	Expect(ParseWords({ "composite", "vendor" }, vendor, error) == ParseResult::Ok, "composite vendor parses");
	Expect(vendor.compositeCheck == CompositeCheck::Vendor, "composite vendor selects the vendor check");

	CheckParse("an unknown composite check is refused", { "composite", "usb" }, ParseResult::Error);
	CheckParse("two composite checks are refused", { "composite", "cdc", "hid" }, ParseResult::Error);
}


void CheckSerial() {
	Options options;
	std::string error;
	Expect(ParseWords({ "storage", "--serial", "0A1B2C" }, options, error) == ParseResult::Ok, "--serial with a separate value");
	Expect(options.serial == "0A1B2C", "--serial takes the value after it");

	Options joined;
	Expect(ParseWords({ "--serial=0A1B2C", "dfu" }, joined, error) == ParseResult::Ok, "--serial=value parses");
	Expect(joined.serial == "0A1B2C", "--serial=value takes the value");
	Expect(joined.command == Command::Dfu, "--serial before the command still selects it");

	CheckParse("--serial needs a value", { "storage", "--serial" }, ParseResult::Error);
	CheckParse("--serial= needs a value", { "storage", "--serial=" }, ParseResult::Error);
	CheckParse("--serial followed by an option is refused", { "storage", "--serial", "--dfu-util", "x" }, ParseResult::Error);
	CheckParse("list refuses --serial", { "list", "--serial", "ABC" }, ParseResult::Error);
}


void CheckDfuOptions() {
	Options options;
	std::string error;
	Expect(
		ParseWords({ "dfu", "--dfu-util", "C:/tools/dfu-util.exe", "--console", "com7" }, options, error) == ParseResult::Ok,
		"dfu takes --dfu-util and --console");
	Expect(options.dfuUtil == "C:/tools/dfu-util.exe", "--dfu-util keeps the path");
	Expect(options.console == "COM7", "--console is kept in upper case");

	CheckParse("--console needs a COM port name", { "dfu", "--console", "USB0" }, ParseResult::Error);
	CheckParse("--console needs a number", { "dfu", "--console", "COM" }, ParseResult::Error);
	CheckParse("--dfu-util applies to dfu only", { "composite", "--dfu-util", "x" }, ParseResult::Error);
	CheckParse("--console applies to dfu only", { "storage", "--console", "COM7" }, ParseResult::Error);
	CheckParse("--dfu-util needs a value", { "dfu", "--dfu-util" }, ParseResult::Error);
	CheckParse("unknown option is refused", { "storage", "--nope" }, ParseResult::Error);
	CheckParse("help is recognised", { "--help" }, ParseResult::Help);
}


}


int main() {
	CheckCommands();
	CheckCompositeChecks();
	CheckSerial();
	CheckDfuOptions();

	std::printf("usbclasscheck options test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
