#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "DeviceSelection.h"
#include "UsbBackend.h"

namespace {


int checks = 0;
int failures = 0;


void Expect(bool condition, const char* what) {
	checks++;
	if (!condition) {
		failures++;
		std::printf("FAIL: %s\n", what);
	}
}


UsbDeviceInfo MakeDevice(const std::string& serial) {
	UsbDeviceInfo info;
	info.serial = serial;
	info.location = "bus 1 port 2";
	return info;
}


UsbDeviceInfo MakeUnreadable() {
	UsbDeviceInfo info;
	info.location = "bus 1 port 3";
	info.problem = "the device reports no serial number";
	return info;
}


void CheckSingleWithoutSerial() {
	std::vector<UsbDeviceInfo> none;
	std::string chosen = "stale";
	Expect(SelectSingleDevice(none, "", chosen) == SingleSelection::NoDevice, "no device and no serial is NoDevice");

	std::vector<UsbDeviceInfo> one;
	one.push_back(MakeDevice("0A1B"));
	chosen = "stale";
	Expect(SelectSingleDevice(one, "", chosen) == SingleSelection::Chosen, "one device without --serial is chosen");
	Expect(chosen.empty(), "one device without --serial keeps the first-device behaviour (empty serial)");

	std::vector<UsbDeviceInfo> oneUnreadable;
	oneUnreadable.push_back(MakeUnreadable());
	chosen.clear();
	Expect(SelectSingleDevice(oneUnreadable, "", chosen) == SingleSelection::Chosen, "one unreadable device is still the first-device case");

	std::vector<UsbDeviceInfo> two;
	two.push_back(MakeDevice("0A1B"));
	two.push_back(MakeDevice("0C3D"));
	chosen.clear();
	Expect(SelectSingleDevice(two, "", chosen) == SingleSelection::Several, "two devices without --serial are refused");

	std::vector<UsbDeviceInfo> mixed;
	mixed.push_back(MakeDevice("0A1B"));
	mixed.push_back(MakeUnreadable());
	chosen.clear();
	Expect(SelectSingleDevice(mixed, "", chosen) == SingleSelection::Several, "an unreadable second device still makes it several");
}


void CheckSingleWithSerial() {
	std::vector<UsbDeviceInfo> two;
	two.push_back(MakeDevice("0A1B"));
	two.push_back(MakeDevice("0C3D"));

	std::string chosen;
	Expect(SelectSingleDevice(two, "0C3D", chosen) == SingleSelection::Chosen, "--serial picks the device that reports it");
	Expect(chosen == "0C3D", "--serial returns the serial it chose");

	chosen.clear();
	Expect(SelectSingleDevice(two, "FFFF", chosen) == SingleSelection::SerialNotFound, "an unknown serial is not found");

	std::vector<UsbDeviceInfo> none;
	chosen.clear();
	Expect(SelectSingleDevice(none, "0A1B", chosen) == SingleSelection::SerialNotFound, "a serial with no devices is not found");

	std::vector<UsbDeviceInfo> shared;
	shared.push_back(MakeDevice("0A1B"));
	shared.push_back(MakeDevice("0A1B"));
	chosen.clear();
	Expect(SelectSingleDevice(shared, "0A1B", chosen) == SingleSelection::SerialShared, "two devices with one serial are refused");

	std::vector<UsbDeviceInfo> withUnreadable;
	withUnreadable.push_back(MakeUnreadable());
	withUnreadable.push_back(MakeDevice("0A1B"));
	chosen.clear();
	Expect(SelectSingleDevice(withUnreadable, "0A1B", chosen) == SingleSelection::Chosen, "an unreadable device does not block a readable one");
}


void CheckAll() {
	std::vector<UsbDeviceInfo> none;
	Expect(SelectAllDevices(none) == AllSelection::NoDevice, "--all with no device is NoDevice");

	std::vector<UsbDeviceInfo> unique;
	unique.push_back(MakeDevice("0A1B"));
	unique.push_back(MakeDevice("0C3D"));
	Expect(SelectAllDevices(unique) == AllSelection::Chosen, "--all with unique serials is chosen");

	std::vector<UsbDeviceInfo> unreadable;
	unreadable.push_back(MakeDevice("0A1B"));
	unreadable.push_back(MakeUnreadable());
	Expect(SelectAllDevices(unreadable) == AllSelection::NoSerial, "--all refuses a device without a serial");

	std::vector<UsbDeviceInfo> shared;
	shared.push_back(MakeDevice("0A1B"));
	shared.push_back(MakeDevice("0A1B"));
	Expect(SelectAllDevices(shared) == AllSelection::SerialShared, "--all refuses two devices with one serial");
}


void CheckCount() {
	std::vector<UsbDeviceInfo> devices;
	devices.push_back(MakeDevice("0A1B"));
	devices.push_back(MakeDevice("0A1B"));
	devices.push_back(MakeDevice("0C3D"));
	Expect(CountSerial(devices, "0A1B") == 2, "CountSerial counts every match");
	Expect(CountSerial(devices, "0C3D") == 1, "CountSerial counts a single match");
	Expect(CountSerial(devices, "FFFF") == 0, "CountSerial counts no match");
}


void CheckSerialCase() {
	Expect(SameSerial("0a1b2c", "0A1B2C"), "serials match ignoring case");
	Expect(SameSerial("0A1B2C", "0A1B2C"), "identical serials match");
	Expect(!SameSerial("0A1B2C", "0A1B2D"), "different serials do not match");
	Expect(!SameSerial("0A1B2C", "0A1B2"), "a prefix is not a match");
	Expect(!SameSerial("", "0A"), "an empty serial is not a match");

	std::vector<UsbDeviceInfo> devices;
	devices.push_back(MakeDevice("0A1B2C"));
	devices.push_back(MakeDevice("0C3D4E"));
	Expect(CountSerial(devices, "0a1b2c") == 1, "CountSerial ignores case");

	std::string chosen;
	Expect(SelectSingleDevice(devices, "0c3d4e", chosen) == SingleSelection::Chosen, "--serial in lower case picks the board");
	Expect(chosen == "0c3d4e", "--serial keeps the text that was typed");

	std::vector<UsbDeviceInfo> shared;
	shared.push_back(MakeDevice("0A1B2C"));
	shared.push_back(MakeDevice("0a1b2c"));
	Expect(SelectAllDevices(shared) == AllSelection::SerialShared, "--all refuses serials that differ only in case");
	chosen.clear();
	Expect(SelectSingleDevice(shared, "0A1B2C", chosen) == SingleSelection::SerialShared, "a serial shared in another case is refused");
}


void CheckOpenFailureText() {
	OpenFailures absent;
	absent.serial = "0A1B2C";
	std::string text = OpenFailureText(absent);
	Expect(text.find("0A1B2C") != std::string::npos && text.find("not attached") != std::string::npos, "no board matched and all were read says not attached");
	Expect(text.find("cannot tell") == std::string::npos, "an absent serial is not reported as unknown");

	OpenFailures matched;
	matched.serial = "0A1B2C";
	matched.matchError = "cannot claim interface 0: access denied";
	matched.readError = "cannot open the benchmark device: busy";
	matched.unreadable = 1;
	Expect(OpenFailureText(matched) == "cannot claim interface 0: access denied", "a matching board that failed reports its own error");

	OpenFailures unread;
	unread.serial = "0A1B2C";
	unread.readError = "cannot open the benchmark device: busy";
	unread.unreadable = 2;
	text = OpenFailureText(unread);
	Expect(text.find("cannot tell") != std::string::npos, "a board that could not be read keeps the open error");
	Expect(text.find("2 benchmark device(s)") != std::string::npos, "the message counts the unreadable boards");
	Expect(text.find("busy") != std::string::npos, "the message says why the board could not be read");
	Expect(text.find("not attached") == std::string::npos, "an unread board is not reported as absent");

	OpenFailures anyBoard;
	anyBoard.readError = "cannot open the benchmark device: busy";
	Expect(OpenFailureText(anyBoard) == "cannot open the benchmark device: busy", "without a serial the open error is reported");

	OpenFailures nothing;
	Expect(OpenFailureText(nothing) == NoDeviceMessage(), "without a serial and no error the device is not attached");
}


}


int main() {
	CheckSingleWithoutSerial();
	CheckSingleWithSerial();
	CheckAll();
	CheckCount();
	CheckSerialCase();
	CheckOpenFailureText();

	std::printf("usbbench device selection test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
