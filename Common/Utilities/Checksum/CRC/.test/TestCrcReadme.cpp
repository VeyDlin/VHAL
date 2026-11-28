#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstdio>
#include <span>


// The Resume example of the CRC wiki page, run as the page writes it. The value the example gives in its comment is checked.

namespace {


int failures = 0;


void ResumeExample() {
	const std::array<uint8, 4> head = {0x31, 0x32, 0x33, 0x34};
	const std::array<uint8, 5> tail = {0x35, 0x36, 0x37, 0x38, 0x39};

	// The CRC of the first part is kept as a finished value
	Checksum::Crc16Modbus first;
	first.Add(head);
	const uint16 saved = first.Get();

	// The object is rebuilt from the saved value and takes the rest of the message
	Checksum::Crc16Modbus crc = Checksum::Crc16Modbus::Resume(saved);
	crc.Add(tail);
	const uint16 value = crc.Get(); // 0x4B37, the CRC of "123456789"

	if (value != 0x4B37) {
		failures++;
		std::printf("TestCrcReadme.cpp: Resume gives 0x%04X, expected 0x4B37\n", static_cast<unsigned int>(value));
	}
}
}


int main() {
	ResumeExample();

	if (failures != 0) {
		std::printf("%d CRC readme check(s) failed\n", failures);
		return 1;
	}

	std::printf("CRC readme examples passed\n");
	return 0;
}
