#include "CheckData.h"

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "DemoProducts.h"
#include "Text.h"

namespace {

// The VID:PID text that dfu-util takes with -d, such as 1209:0004.
std::string DfuDeviceText(std::uint16_t productId) {
	std::string device;
	AppendFormat(
		device,
		"%04X:%04X",
		static_cast<unsigned int>(DemoProducts::VendorId),
		static_cast<unsigned int>(productId));
	return device;
}

}


std::uint64_t StorageTestFileSize(std::uint64_t freeBytes, std::uint64_t sectorBytes) {
	if (sectorBytes == 0 || freeBytes < 2 * sectorBytes) {
		return 0;
	}

	// The cap is a whole number of sectors too, so a sector size that does not divide the maximum still gives whole sectors.
	std::uint64_t cap = StorageTestMaxBytes / sectorBytes * sectorBytes;
	std::uint64_t size = (freeBytes - sectorBytes) / sectorBytes * sectorBytes;
	if (size > cap) {
		size = cap;
	}

	return size;
}


std::string StorageReadmePhrase() {
	return DemoProducts::StorageReadmeTitle;
}


std::string StorageInfoPhrase() {
	std::string text;
	AppendFormat(
		text,
		"USB ID: %04X:%04X",
		static_cast<unsigned int>(DemoProducts::VendorId),
		static_cast<unsigned int>(DemoProducts::StorageProductId));
	return text;
}


bool IsStorageDemo(const std::string& vendor, const std::string& product) {
	return TrimSpaces(vendor) == DemoProducts::StorageVendor && TrimSpaces(product) == DemoProducts::StorageProduct;
}


void FillPattern(std::vector<std::uint8_t>& data, std::uint32_t seed) {
	std::mt19937 generator(seed);
	for (std::uint8_t& value : data) {
		value = static_cast<std::uint8_t>(generator() & 0xFF);
	}
}


bool FirstMismatch(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual, std::size_t& offset) {
	std::size_t common = expected.size() < actual.size() ? expected.size() : actual.size();
	for (std::size_t index = 0; index < common; index++) {
		if (expected[index] != actual[index]) {
			offset = index;
			return true;
		}
	}

	if (expected.size() != actual.size()) {
		offset = common;
		return true;
	}

	return false;
}


std::vector<std::string> DfuArguments(
	std::uint16_t productId,
	const std::string& serial,
	DfuDirection direction,
	const std::string& filePath)
{
	std::vector<std::string> arguments;
	arguments.push_back("-d");
	arguments.push_back(DfuDeviceText(productId));
	arguments.push_back("-a");
	arguments.push_back("0");
	if (!serial.empty()) {
		arguments.push_back("-S");
		arguments.push_back(serial);
	}

	arguments.push_back(direction == DfuDirection::Download ? "-D" : "-U");
	arguments.push_back(filePath);
	return arguments;
}


std::vector<std::string> DfuListArguments(std::uint16_t productId) {
	return { "-l", "-d", DfuDeviceText(productId) };
}


std::vector<std::string> DfuListedSerials(const std::string& output) {
	const std::string marker = "serial=\"";
	std::vector<std::string> serials;
	std::size_t position = 0;
	while ((position = output.find(marker, position)) != std::string::npos) {
		std::size_t start = position + marker.size();
		std::size_t end = output.find('"', start);
		if (end == std::string::npos) {
			break;
		}

		serials.push_back(output.substr(start, end - start));
		position = end + 1;
	}

	return serials;
}


bool PickDfuSerial(const std::string& listOutput, const std::string& boardSerial, std::string& serial, std::string& error) {
	serial.clear();
	error.clear();
	if (boardSerial.empty()) {
		error = "the board has no serial number, so there is no serial to pick";
		return false;
	}

	std::vector<std::string> matches;
	for (const std::string& listed : DfuListedSerials(listOutput)) {
		if (!EqualsIgnoreCase(listed, boardSerial)) {
			continue;
		}

		bool seen = false;
		for (const std::string& match : matches) {
			if (match == listed) {
				seen = true;
			}
		}

		if (!seen) {
			matches.push_back(listed);
		}
	}

	if (matches.empty()) {
		error = "dfu-util lists no DFU device with the board's serial " + boardSerial;
		return false;
	}

	if (matches.size() > 1) {
		error = "dfu-util lists " + std::to_string(matches.size()) + " DFU devices with the board's serial, so the one to use is not known";
		return false;
	}

	serial = matches[0];
	return true;
}


std::string CommandLineOf(const std::vector<std::string>& arguments) {
	std::string line;
	for (const std::string& argument : arguments) {
		if (!line.empty()) {
			line += " ";
		}

		if (argument.find(' ') != std::string::npos) {
			line += "\"" + argument + "\"";
		} else {
			line += argument;
		}
	}

	return line;
}


DfuOutcome ClassifyDfuRun(int exitCode, const std::string& output) {
	if (output.find("No DFU capable USB device available") != std::string::npos
		|| output.find("Cannot open DFU device") != std::string::npos)
	{
		return DfuOutcome::NoDevice;
	}

	return exitCode == 0 ? DfuOutcome::Ok : DfuOutcome::Failed;
}


bool FindCrcLine(const std::string& text, std::uint32_t bytes, std::uint32_t& crc) {
	std::string upper = UpperAscii(text);
	std::string prefix = "DOWNLOAD COMPLETE BYTES=";
	std::string number;
	AppendFormat(number, "%u", static_cast<unsigned int>(bytes));
	std::string marker = prefix + number + " CRC32=0X";

	std::size_t position = upper.find(marker);
	if (position == std::string::npos) {
		return false;
	}

	std::size_t digits = position + marker.size();
	if (digits + 8 > upper.size()) {
		return false;
	}

	std::uint32_t value = 0;
	for (std::size_t index = digits; index < digits + 8; index++) {
		char digit = upper[index];
		std::uint32_t nibble = 0;
		if (digit >= '0' && digit <= '9') {
			nibble = static_cast<std::uint32_t>(digit - '0');
		} else if (digit >= 'A' && digit <= 'F') {
			nibble = static_cast<std::uint32_t>(digit - 'A' + 10);
		} else {
			return false;
		}

		value = value * 16 + nibble;
	}

	crc = value;
	return true;
}


std::string HexCrc(std::uint32_t crc) {
	std::string text;
	AppendFormat(text, "0x%08X", static_cast<unsigned int>(crc));
	return text;
}
