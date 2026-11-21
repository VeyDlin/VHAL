#include <cstdio>
#include <string>

#include <windows.h>

#include "Commands.h"
#include "ExitCodes.h"
#include "Options.h"

int main(int argc, char** argv) {
	// The report's text is UTF-8: endpoint names, adapter names and Windows messages keep their characters on the console.
	SetConsoleOutputCP(CP_UTF8);

	Options options;
	std::string error;
	ParseResult parsed = ParseOptions(argc, argv, options, error);
	if (parsed == ParseResult::Help) {
		std::fputs(UsageText().c_str(), stdout);
		return ExitOk;
	}

	if (parsed == ParseResult::Error) {
		std::fprintf(stderr, "usbclasscheck: %s\n\n", error.c_str());
		std::fputs(UsageText().c_str(), stderr);
		return ExitUsage;
	}

	return RunCommand(options);
}
