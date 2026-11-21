#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The line coding a CDC-ACM host sets, with the names the status line prints: parity none, odd, even, mark or space, and stop
// bits 1, 1.5 or 2.
struct LineCoding {
	std::uint32_t baud = 0;
	std::uint32_t dataBits = 0;
	std::string parity;
	std::string stopBits;
};

// The fields of the status command, as the composite prints them (CompositeCommandLine.cpp).
struct CompositeStatus {
	std::string state;
	std::uint32_t address = 0;
	bool numLock = false;
	bool capsLock = false;
	bool scrollLock = false;
	LineCoding coding;
	bool dtr = false;
	std::uint32_t looped = 0;
	std::uint32_t cdcDropped = 0;
	std::uint32_t usbErrors = 0;
};

// The prompt the command line prints after every answer.
inline constexpr const char* PromptText = "> ";

// True once the answer ends with the prompt on a line of its own. Until then more bytes may still be on the way.
bool AnswerComplete(const std::string& answer);

// The text split at line ends (CR LF, LF, or CR). The prompt is the last line of a complete answer.
std::vector<std::string> SplitLines(const std::string& text);

// The body of the answer to one command: the lines between the echo of the command (the first line) and the prompt (the last).
bool CommandBody(
	const std::string& answer,
	const std::string& command,
	std::vector<std::string>& body,
	std::string& error);

// The fields of the status answer: every key=value word in the body. Fails when a field is missing or malformed.
bool ParseStatus(const std::vector<std::string>& body, CompositeStatus& status, std::string& error);

bool SameLineCoding(const LineCoding& reported, const LineCoding& expected);

// The coding as the usual shorthand: 57600 8N1, 9600 7E1.
std::string DescribeLineCoding(const LineCoding& coding);
