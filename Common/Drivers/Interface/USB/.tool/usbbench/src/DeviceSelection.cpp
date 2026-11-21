#include "DeviceSelection.h"

#include <cstddef>
#include <string>
#include <vector>

#include "Text.h"
#include "UsbBackend.h"

namespace {

char LowerAscii(char character) {
	if (character >= 'A' && character <= 'Z') {
		return static_cast<char>(character - 'A' + 'a');
	}

	return character;
}

}


SingleSelection SelectSingleDevice(
	const std::vector<UsbDeviceInfo>& devices,
	const std::string& serial,
	std::string& chosenSerial)
{
	if (!serial.empty()) {
		std::size_t matches = CountSerial(devices, serial);
		if (matches == 0) {
			return SingleSelection::SerialNotFound;
		}

		if (matches > 1) {
			return SingleSelection::SerialShared;
		}

		chosenSerial = serial;
		return SingleSelection::Chosen;
	}

	if (devices.empty()) {
		return SingleSelection::NoDevice;
	}

	if (devices.size() > 1) {
		return SingleSelection::Several;
	}

	chosenSerial.clear();
	return SingleSelection::Chosen;
}


AllSelection SelectAllDevices(const std::vector<UsbDeviceInfo>& devices) {
	if (devices.empty()) {
		return AllSelection::NoDevice;
	}

	// Each device is opened by its serial number, so every one must have a readable serial, and no two may share it.
	for (const UsbDeviceInfo& device : devices) {
		if (device.serial.empty()) {
			return AllSelection::NoSerial;
		}

		if (CountSerial(devices, device.serial) > 1) {
			return AllSelection::SerialShared;
		}
	}

	return AllSelection::Chosen;
}


std::size_t CountSerial(const std::vector<UsbDeviceInfo>& devices, const std::string& serial) {
	std::size_t matches = 0;
	for (const UsbDeviceInfo& device : devices) {
		if (SameSerial(device.serial, serial)) {
			matches++;
		}
	}

	return matches;
}


bool SameSerial(const std::string& first, const std::string& second) {
	if (first.size() != second.size()) {
		return false;
	}

	for (std::size_t index = 0; index < first.size(); index++) {
		if (LowerAscii(first[index]) != LowerAscii(second[index])) {
			return false;
		}
	}

	return true;
}


std::string OpenFailureText(const OpenFailures& failures) {
	if (!failures.matchError.empty()) {
		return failures.matchError;
	}

	if (failures.serial.empty()) {
		return failures.readError.empty() ? NoDeviceMessage() : failures.readError;
	}

	if (failures.unreadable > 0) {
		std::string text;
		AppendFormat(
			text,
			"cannot tell whether serial '%s' is attached: %u benchmark device(s) could not be read (%s)",
			failures.serial.c_str(),
			static_cast<unsigned int>(failures.unreadable),
			failures.readError.c_str());
		return text;
	}

	return NoDeviceMessage(failures.serial);
}
