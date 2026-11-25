#include "SimCheck.h"
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>


// The nine check bytes "123456789". Each catalogue check value below is the CRC of these bytes.
inline constexpr std::array<uint8, 9> checkDigits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// CRC-16/SPI-FUJITSU written as a chain: polynomial 1021, initial 1D0F, not reflected, final XOR 0000, check E5CC. The unchecked form
// is the same chain without Check.
inline constexpr Checksum::CrcParameters spiFujitsu = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false)
	.Check(0xE5CC);

inline constexpr Checksum::CrcParameters spiFujitsuUnchecked = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false);


// CRC-16/AUG-CCITT has the same parameters as SPI-FUJITSU under its own name. Its chain uses the defaults of the reflections.
inline constexpr Checksum::CrcParameters augCcitt = Checksum::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.Check(0xE5CC);


// CRC-8/DARC: polynomial 39, initial 00, reflected in and out, final XOR 00, check 15. The unchecked form is the same chain without Check.
inline constexpr Checksum::CrcParameters darc = Checksum::CrcParameters()
	.Width(8)
	.Polynomial(0x39)
	.Initial(0x00)
	.FinalXor(0x00)
	.ReflectIn()
	.ReflectOut()
	.Check(0x15);

inline constexpr Checksum::CrcParameters darcUnchecked = Checksum::CrcParameters()
	.Width(8)
	.Polynomial(0x39)
	.Initial(0x00)
	.FinalXor(0x00)
	.ReflectIn()
	.ReflectOut();


// CRC-32/JAMCRC: polynomial 04C11DB7, initial FFFFFFFF, reflected in and out, final XOR 00000000, check 340BC6D9. The unchecked form is
// the same chain without Check.
inline constexpr Checksum::CrcParameters jamcrc = Checksum::CrcParameters()
	.Width(32)
	.Polynomial(0x04C11DB7)
	.Initial(0xFFFFFFFF)
	.FinalXor(0x00000000)
	.ReflectIn()
	.ReflectOut()
	.Check(0x340BC6D9);

inline constexpr Checksum::CrcParameters jamcrcUnchecked = Checksum::CrcParameters()
	.Width(32)
	.Polynomial(0x04C11DB7)
	.Initial(0xFFFFFFFF)
	.FinalXor(0x00000000)
	.ReflectIn()
	.ReflectOut();


// Encodes the nine check bytes with the transform, then checks the CRC bytes after them against crcBytes (least significant first, as the
// low byte first order puts them), and decodes the packet back to the nine bytes.
template<typename Transform>
bool EncodesCheckDigitsAs(const std::array<uint8, Transform::growth>& crcBytes) {
	constexpr std::size_t Growth = Transform::growth;
	Transform transform;
	VUB::Meta meta{};
	std::array<uint8, 9 + Growth> wire{};
	Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(checkDigits.data(), checkDigits.size()), wire);
	if (encoded.IsErr() || encoded.Value() != 9 + Growth) {
		return false;
	}

	for (std::size_t index = 0; index < Growth; index++) {
		if (wire[9 + index] != crcBytes[index]) {
			return false;
		}
	}

	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	return decoded.IsOk() && decoded.Value() == 9;
}


// CRC-16/SPI-FUJITSU through the chain, with the check value and without it, with the bitwise engine (the default) and the table engine:
// the nine digits are followed by E5CC, low byte first (CC E5).
void TestSpiFujitsuThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<spiFujitsu>>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<spiFujitsu>::With<Checksum::CrcTable>>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<spiFujitsuUnchecked>>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<spiFujitsuUnchecked>::With<Checksum::CrcTable>>>({0xCC, 0xE5})));
}


// CRC-16/AUG-CCITT through the chain, with the check value, with each engine.
void TestAugCcittThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<augCcitt>>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<augCcitt>::With<Checksum::CrcTable>>>({0xCC, 0xE5})));
}


// CRC-8/DARC through the chain: one byte, 15, with and without the check value, with each engine.
void TestDarcThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<darc>>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<darc>::With<Checksum::CrcTable>>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<darcUnchecked>>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<darcUnchecked>::With<Checksum::CrcTable>>>({0x15})));
}


// CRC-32/JAMCRC through the chain: 340BC6D9, so the bytes are D9 C6 0B 34, with and without the check value, with each engine.
void TestJamcrcThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<jamcrc>>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<jamcrc>::With<Checksum::CrcTable>>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<jamcrcUnchecked>>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<Checksum::Crc<jamcrcUnchecked>::With<Checksum::CrcTable>>>({0xD9, 0xC6, 0x0B, 0x34})));
}


// The contract check of the test kit on one chained CRC: JAMCRC, unchecked, with the bitwise engine.
void TestContractOnChainedJamcrc() {
	VUB::Crc<Checksum::Crc<jamcrcUnchecked>> transform;
	SIM_EXPECT(VUB::CheckTransform(transform, 51, 300, 40));
}


// The check is not part of the wire, and the engine is not either: the fingerprint is the same with and without the check value, and
// with each engine. The chain of a preset gives the preset's fingerprint.
void TestFingerprintIgnoresCheckAndEngine() {
	SIM_EXPECT((VUB::Crc<Checksum::Crc<spiFujitsu>>::fingerprint == VUB::Crc<Checksum::Crc<spiFujitsuUnchecked>>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc<spiFujitsu>>::fingerprint == VUB::Crc<Checksum::Crc<spiFujitsu>::With<Checksum::CrcTable>>::fingerprint));
	SIM_EXPECT((VUB::Crc<Checksum::Crc16Modbus>::fingerprint == VUB::Crc<Checksum::Crc16Modbus::With<Checksum::CrcTable>>::fingerprint));
}


void TestChainGivesThePresetFingerprint() {
	SIM_EXPECT((VUB::Crc<Checksum::Crc<Checksum::CrcParameters()
		.Width(16)
		.Polynomial(0x8005)
		.Initial(0xFFFF)
		.FinalXor(0x0000)
		.ReflectIn()
		.ReflectOut()
		.Check(0x4B37)>>::fingerprint == VUB::Crc<Checksum::Crc16Modbus>::fingerprint));
}


int main() {
	TestSpiFujitsuThroughTheChain();
	TestAugCcittThroughTheChain();
	TestDarcThroughTheChain();
	TestJamcrcThroughTheChain();
	TestContractOnChainedJamcrc();
	TestFingerprintIgnoresCheckAndEngine();
	TestChainGivesThePresetFingerprint();
	return SimFinish("TestVubCrcChain");
}
