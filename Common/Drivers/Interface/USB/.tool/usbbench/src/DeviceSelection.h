#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "UsbBackend.h"

// Outcome of picking the one device that a single-device command acts on.
enum class SingleSelection : std::uint8_t {
	Chosen,
	NoDevice,
	SerialNotFound,
	SerialShared,
	Several
};

// Outcome of picking every device for --all.
enum class AllSelection : std::uint8_t {
	Chosen,
	NoDevice,
	NoSerial,
	SerialShared
};

// What a backend learned while it looked for the wanted device and found none it could open. A backend fills it in as it
// goes; OpenFailureText turns it into the message Open returns.
struct OpenFailures {
	// The serial that was asked for. Empty when any device would do.
	std::string serial;
	// The error of a device that is the wanted one (or any device, without a serial) but could not be opened or claimed.
	std::string matchError;
	// The error of the last device that could not be opened or read.
	std::string readError;
	// Devices whose serial could not be read. Any of them might have been the wanted one.
	std::size_t unreadable = 0;
};


// Picks the device for a single-device command. Without --serial, one attached device is used as before: chosenSerial is
// left empty, and Open takes the first device it finds. Several devices without a serial are refused, because a run on
// the wrong board would still print a result. With a serial, exactly one device must report it.
SingleSelection SelectSingleDevice(
	const std::vector<UsbDeviceInfo>& devices,
	const std::string& serial,
	std::string& chosenSerial);

// Checks that every device can be addressed by its serial number, which --all needs.
AllSelection SelectAllDevices(const std::vector<UsbDeviceInfo>& devices);

// How many devices report this serial number.
std::size_t CountSerial(const std::vector<UsbDeviceInfo>& devices, const std::string& serial);

// Serial numbers are compared ignoring ASCII case: the boards print hex, and the user may type either case.
bool SameSerial(const std::string& first, const std::string& second);

// The message Open returns when it opened no device. A device that was the wanted one and failed is reported as it failed.
// A wanted serial whose device might be among the unreadable ones is not reported as absent, because that is not known.
// Otherwise the serial is not attached.
std::string OpenFailureText(const OpenFailures& failures);
