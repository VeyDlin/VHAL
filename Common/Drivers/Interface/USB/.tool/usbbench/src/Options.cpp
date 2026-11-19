#include "Options.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "BenchProtocol.h"

namespace {

// Parses a decimal number with an optional k, m or g suffix (binary multiples).
bool ParseNumber(const std::string& text, std::uint64_t& value) {
	const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
	std::size_t position = 0;
	std::uint64_t number = 0;
	while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
		std::uint64_t digit = static_cast<std::uint64_t>(text[position] - '0');
		if (number > (maximum - digit) / 10) {
			return false;
		}

		number = number * 10 + digit;
		position++;
	}

	if (position == 0) {
		return false;
	}

	std::uint64_t multiplier = 1;
	if (position < text.size()) {
		if (position + 1 != text.size()) {
			return false;
		}

		int suffix = std::tolower(static_cast<unsigned char>(text[position]));
		if (suffix == 'k') {
			multiplier = 1024;
		} else if (suffix == 'm') {
			multiplier = 1024 * 1024;
		} else if (suffix == 'g') {
			multiplier = 1024ULL * 1024 * 1024;
		} else {
			return false;
		}
	}

	if (number > maximum / multiplier) {
		return false;
	}

	value = number * multiplier;
	return true;
}


bool ParseBackend(const std::string& text, BackendKind& kind) {
	if (text == "libusb") {
		kind = BackendKind::LibUsb;
		return true;
	}

	if (text == "winusb") {
		kind = BackendKind::WinUsb;
		return true;
	}

	return false;
}


// Sizes of bulk data must be whole packets, so that the device and both backends see the same transfer boundaries.
bool CheckBulkBytes(std::uint64_t bytes, std::string& error) {
	if (bytes == 0) {
		error = "the byte count must be greater than 0";
		return false;
	}

	if (bytes % BenchProtocol::BulkPacketSize != 0) {
		error = "the byte count must be a multiple of " + std::to_string(BenchProtocol::BulkPacketSize);
		return false;
	}

	return true;
}


bool ParseCommandName(const std::string& name, Command& command, bool& takesBytes, bool& takesMilliseconds) {
	takesBytes = false;
	takesMilliseconds = false;
	if (name == "info") {
		command = Command::Info;
		return true;
	}

	if (name == "counters") {
		command = Command::Counters;
		return true;
	}

	if (name == "in") {
		command = Command::In;
		takesBytes = true;
		return true;
	}

	if (name == "out") {
		command = Command::Out;
		takesBytes = true;
		return true;
	}

	if (name == "loopback") {
		command = Command::Loopback;
		takesBytes = true;
		return true;
	}

	if (name == "sourcesink") {
		command = Command::SourceSink;
		takesBytes = true;
		return true;
	}

	if (name == "reconnect") {
		command = Command::Reconnect;
		takesMilliseconds = true;
		return true;
	}

	return false;
}

}


ParseResult ParseOptions(int argc, char** argv, Options& options, std::string& error) {
	std::vector<std::string> positional;
	for (int index = 1; index < argc; index++) {
		std::string argument = argv[index];
		if (argument == "--help" || argument == "-h") {
			return ParseResult::Help;
		}

		if (argument.rfind("--", 0) != 0) {
			positional.push_back(argument);
			continue;
		}

		std::string name = argument;
		std::string value;
		std::size_t equals = argument.find('=');
		if (equals != std::string::npos) {
			name = argument.substr(0, equals);
			value = argument.substr(equals + 1);
		} else {
			if (index + 1 >= argc) {
				error = name + " needs a value";
				return ParseResult::Error;
			}

			index++;
			value = argv[index];
		}

		if (name == "--backend") {
			if (!ParseBackend(value, options.backend)) {
				error = "unknown backend '" + value + "' (use libusb or winusb)";
				return ParseResult::Error;
			}
		} else if (name == "--inflight") {
			std::uint64_t inflight = 0;
			if (!ParseNumber(value, inflight) || inflight < 1 || inflight > MaxInflight) {
				error = "--inflight must be a number from 1 to " + std::to_string(MaxInflight);
				return ParseResult::Error;
			}

			options.inflight = static_cast<std::uint32_t>(inflight);
		} else if (name == "--transfer") {
			std::uint64_t transfer = 0;
			if (!ParseNumber(value, transfer) || transfer > MaxTransferBytes) {
				error = "--transfer must be at most " + std::to_string(MaxTransferBytes) + " bytes";
				return ParseResult::Error;
			}

			if (transfer == 0 || transfer % BenchProtocol::BulkPacketSize != 0) {
				error = "--transfer must be a multiple of " + std::to_string(BenchProtocol::BulkPacketSize);
				return ParseResult::Error;
			}

			options.transferSize = static_cast<std::size_t>(transfer);
		} else {
			error = "unknown option " + name;
			return ParseResult::Error;
		}
	}

	if (positional.empty()) {
		error = "no command given";
		return ParseResult::Error;
	}

	bool takesBytes = false;
	bool takesMilliseconds = false;
	if (!ParseCommandName(positional[0], options.command, takesBytes, takesMilliseconds)) {
		error = "unknown command '" + positional[0] + "'";
		return ParseResult::Error;
	}

	std::size_t expectedPositional = 1;
	if (takesBytes || takesMilliseconds) {
		expectedPositional = 2;
	}

	if (positional.size() != expectedPositional) {
		const char* expected = "no argument";
		if (takesBytes) {
			expected = "a byte count";
		} else if (takesMilliseconds) {
			expected = "a time in ms";
		}

		error = "'" + positional[0] + "' takes " + expected;
		return ParseResult::Error;
	}

	if (takesBytes) {
		if (!ParseNumber(positional[1], options.bytes)) {
			error = "'" + positional[1] + "' is not a byte count";
			return ParseResult::Error;
		}

		if (!CheckBulkBytes(options.bytes, error)) {
			return ParseResult::Error;
		}
	}

	if (takesMilliseconds) {
		if (!ParseNumber(positional[1], options.reconnectMs) || options.reconnectMs > MaxReconnectMs) {
			error = "reconnect time must be a number of ms from 0 to " + std::to_string(MaxReconnectMs);
			return ParseResult::Error;
		}
	}

	return ParseResult::Ok;
}


std::string UsageText() {
	return
		"usbbench - throughput and data-check benchmark for the VHAL USB benchmark firmware\n"
		"\n"
		"Usage: usbbench <command> [argument] [options]\n"
		"\n"
		"Commands:\n"
		"  info                 descriptors of the benchmark device\n"
		"  counters             counters kept by the device\n"
		"  in <bytes>           device sends the pattern, host checks it\n"
		"  out <bytes>          host sends the pattern, device checks it (see device counters)\n"
		"  loopback <bytes>     device returns the host data, host checks it\n"
		"  sourcesink <bytes>   both directions at once, each checked\n"
		"  reconnect <ms>       pull-up off for <ms>; prints the time until the device is back\n"
		"\n"
		"Options:\n"
		"  --backend libusb|winusb   libusb (default, all platforms) or native WinUSB (Windows)\n"
		"  --inflight N              transfers in flight per direction, 1 to 32 (default 8)\n"
		"  --transfer bytes          bytes per transfer, a multiple of 64, up to 1M (default 64k)\n"
		"  --help                    this text\n"
		"\n"
		"Sizes accept k, m and g suffixes (binary). <bytes> must be a multiple of 64.\n"
		"Exit codes: 0 pass, 1 device or transport failure, 2 usage error, 3 data error.\n";
}
