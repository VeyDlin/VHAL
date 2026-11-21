#pragma once

#include <cstdint>
#include <string>

enum class Command : std::uint8_t {
	None,
	List,
	Composite,
	Storage,
	Dfu,
	Network,
	Audio
};

// Which checks of the composite run. All runs the three in order.
enum class CompositeCheck : std::uint8_t {
	All,
	Cdc,
	Hid,
	Vendor
};

enum class ParseResult : std::uint8_t {
	Ok,
	Help,
	Error
};

struct Options {
	Command command = Command::None;
	CompositeCheck compositeCheck = CompositeCheck::All;
	std::string serial;
	std::string dfuUtil;
	std::string console;
};

ParseResult ParseOptions(int argc, char** argv, Options& options, std::string& error);

std::string UsageText();
