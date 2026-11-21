#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Crc32.h"

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


void CheckKnownValues() {
	const char* standard = "123456789";
	const std::uint8_t* standardBytes = reinterpret_cast<const std::uint8_t*>(standard);
	Expect(Crc32(standardBytes, std::strlen(standard)) == 0xCBF43926U, "the check value of \"123456789\" is 0xCBF43926");

	Expect(Crc32(nullptr, 0) == 0, "the CRC of nothing is 0");

	const char* fox = "The quick brown fox jumps over the lazy dog";
	const std::uint8_t* foxBytes = reinterpret_cast<const std::uint8_t*>(fox);
	Expect(Crc32(foxBytes, std::strlen(fox)) == 0x414FA339U, "the CRC of the fox sentence is 0x414FA339");
}


void CheckVectorOverload() {
	std::vector<std::uint8_t> bytes = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
	Expect(Crc32(bytes) == 0xCBF43926U, "the vector overload gives the same CRC");
}


}


int main() {
	CheckKnownValues();
	CheckVectorOverload();

	std::printf("usbclasscheck crc32 test: %d checks, %d failures\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
