#include "Options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "Text.h"

namespace {

// The COM port name the console option takes: COM followed by the number, in any case.
bool IsComPortName(const std::string& text) {
	std::string upper = UpperAscii(text);
	if (upper.size() < 4 || upper.compare(0, 3, "COM") != 0) {
		return false;
	}

	for (std::size_t index = 3; index < upper.size(); index++) {
		if (upper[index] < '0' || upper[index] > '9') {
			return false;
		}
	}

	return true;
}


bool ParseCompositeCheck(const std::string& word, CompositeCheck& check) {
	if (word == "cdc") {
		check = CompositeCheck::Cdc;
		return true;
	}

	if (word == "hid") {
		check = CompositeCheck::Hid;
		return true;
	}

	if (word == "vendor") {
		check = CompositeCheck::Vendor;
		return true;
	}

	return false;
}


bool ParseCommandName(const std::string& name, Command& command) {
	if (name == "list") {
		command = Command::List;
		return true;
	}

	if (name == "composite") {
		command = Command::Composite;
		return true;
	}

	if (name == "storage") {
		command = Command::Storage;
		return true;
	}

	if (name == "dfu") {
		command = Command::Dfu;
		return true;
	}

	if (name == "network") {
		command = Command::Network;
		return true;
	}

	if (name == "audio") {
		command = Command::Audio;
		return true;
	}

	return false;
}


// The options that only some commands take. They are checked once the command is known.
bool CheckOptionChoice(const Options& options, std::string& error) {
	if (options.command == Command::List && (!options.serial.empty() || !options.dfuUtil.empty() || !options.console.empty())) {
		error = "list takes no options: it shows every attached demo board";
		return false;
	}

	if (!options.dfuUtil.empty() && options.command != Command::Dfu) {
		error = "--dfu-util applies to dfu only";
		return false;
	}

	if (!options.console.empty() && options.command != Command::Dfu) {
		error = "--console applies to dfu only";
		return false;
	}

	return true;
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

		// A value that starts with -- is another option, so the option before it has no value (for example --serial --dfu-util).
		if (value.empty() || value.rfind("--", 0) == 0) {
			error = name + " needs a value, not '" + value + "'";
			return ParseResult::Error;
		}

		if (name == "--serial") {
			options.serial = value;
		} else if (name == "--dfu-util") {
			options.dfuUtil = value;
		} else if (name == "--console") {
			if (!IsComPortName(value)) {
				error = "--console needs a COM port name, for example COM7";
				return ParseResult::Error;
			}

			options.console = UpperAscii(value);
		} else {
			error = "unknown option " + name;
			return ParseResult::Error;
		}
	}

	if (positional.empty()) {
		error = "no command given";
		return ParseResult::Error;
	}

	if (!ParseCommandName(positional[0], options.command)) {
		error = "unknown command '" + positional[0] + "'";
		return ParseResult::Error;
	}

	if (options.command == Command::Composite) {
		if (positional.size() == 2) {
			if (!ParseCompositeCheck(positional[1], options.compositeCheck)) {
				error = "composite takes cdc, hid or vendor, not '" + positional[1] + "'";
				return ParseResult::Error;
			}
		} else if (positional.size() > 2) {
			error = "composite takes one check name at most";
			return ParseResult::Error;
		}
	} else if (positional.size() != 1) {
		error = "'" + positional[0] + "' takes no argument";
		return ParseResult::Error;
	}

	if (!CheckOptionChoice(options, error)) {
		return ParseResult::Error;
	}

	return ParseResult::Ok;
}


std::string UsageText() {
	return
		"usbclasscheck - checks the USB classes of the STM32G4 demo boards end to end, on a real board\n"
		"\n"
		"Usage: usbclasscheck <command> [check] [options]\n"
		"\n"
		"Commands:\n"
		"  list                      every attached demo board with its PID, demo name and serial number\n"
		"  composite [cdc|hid|vendor]\n"
		"                            USBComposite: serial port, keyboard and mouse, vendor loopback.\n"
		"                            All three checks run in order unless one is named.\n"
		"  storage                   USBStorage: the VHAL DEMO volume, its files and a write and read back\n"
		"  dfu                       USBDfu: a download and an upload through dfu-util, compared byte for byte\n"
		"  network                   USBNetwork: the host's adapter, the DHCP lease, echoes and the ARP entry\n"
		"  audio                     USBAudio: the microphone's tone and level, and the speaker's loopback into the microphone\n"
		"\n"
		"Options:\n"
		"  --serial text             the board with this serial number (see list), ignoring case\n"
		"  --dfu-util path           the dfu-util program (dfu only; default: dfu-util on the PATH)\n"
		"  --console COMn            the board's console at 115200 (dfu only); its CRC-32 line is checked\n"
		"  --help                    this text\n"
		"\n"
		"With several boards of the demo attached and no --serial, a command lists them and stops.\n"
		"The composite's hid check presses F13 to F24 and moves the pointer; see README.md.\n"
		"Exit codes: 0 all checks pass, 1 a check could not run, 2 usage error, 3 a check failed.\n";
}
