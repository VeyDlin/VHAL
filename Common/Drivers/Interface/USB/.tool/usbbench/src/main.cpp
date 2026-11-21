#include <cstdio>
#include <string>

#include "Commands.h"
#include "Options.h"

int main(int argc, char** argv) {
	Options options;
	std::string error;
	ParseResult parsed = ParseOptions(argc, argv, options, error);
	if (parsed == ParseResult::Help) {
		std::fputs(UsageText().c_str(), stdout);
		return ExitOk;
	}

	if (parsed == ParseResult::Error) {
		std::fprintf(stderr, "usbbench: %s\n\n", error.c_str());
		std::fputs(UsageText().c_str(), stderr);
		return ExitUsage;
	}

	return RunCommand(options);
}
