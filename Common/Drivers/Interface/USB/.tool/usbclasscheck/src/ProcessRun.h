#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Runs a program with the arguments and waits for it. Its standard output and error are read into output while it runs, so a large
// output does not block it. A program that runs longer than timeoutMs is ended. A bare program name is looked up on the PATH only,
// never in the current folder; a name with a folder part is used as it is. False with an error when the program is not found, could
// not start, or was ended.
bool RunProgram(
	const std::string& program,
	const std::vector<std::string>& arguments,
	std::uint32_t timeoutMs,
	int& exitCode,
	std::string& output,
	std::string& error);
