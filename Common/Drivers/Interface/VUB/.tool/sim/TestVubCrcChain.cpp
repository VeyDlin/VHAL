#include "SimCheck.h"
#include <Drivers/Interface/VUB/Engine/CrcBitwise.h>
#include <Drivers/Interface/VUB/Engine/CrcTable.h>
#include <Drivers/Interface/VUB/Meta.h>
#include <Drivers/Interface/VUB/TestKit/Contracts.h>
#include <Drivers/Interface/VUB/Transform/Crc.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>


// The nine check bytes "123456789". Each catalogue check value below is the CRC of these bytes.
inline constexpr std::array<uint8, 9> checkDigits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// CRC-16/SPI-FUJITSU written as a chain: polynomial 1021, initial 1D0F, not reflected, final XOR 0000, check E5CC. The unchecked form
// is the same chain without Check.
inline constexpr VUB::CrcParameters spiFujitsu = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false)
	.Check(0xE5CC);

inline constexpr VUB::CrcParameters spiFujitsuUnchecked = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.ReflectIn(false)
	.ReflectOut(false);


// CRC-16/AUG-CCITT has the same parameters as SPI-FUJITSU under its own name. Its chain uses the defaults of the reflections.
inline constexpr VUB::CrcParameters augCcitt = VUB::CrcParameters()
	.Width(16)
	.Polynomial(0x1021)
	.Initial(0x1D0F)
	.FinalXor(0x0000)
	.Check(0xE5CC);


// CRC-8/DARC: polynomial 39, initial 00, reflected in and out, final XOR 00, check 15. The unchecked form is the same chain without Check.
inline constexpr VUB::CrcParameters darc = VUB::CrcParameters()
	.Width(8)
	.Polynomial(0x39)
	.Initial(0x00)
	.FinalXor(0x00)
	.ReflectIn()
	.ReflectOut()
	.Check(0x15);

inline constexpr VUB::CrcParameters darcUnchecked = VUB::CrcParameters()
	.Width(8)
	.Polynomial(0x39)
	.Initial(0x00)
	.FinalXor(0x00)
	.ReflectIn()
	.ReflectOut();


// CRC-32/JAMCRC: polynomial 04C11DB7, initial FFFFFFFF, reflected in and out, final XOR 00000000, check 340BC6D9. The unchecked form is
// the same chain without Check.
inline constexpr VUB::CrcParameters jamcrc = VUB::CrcParameters()
	.Width(32)
	.Polynomial(0x04C11DB7)
	.Initial(0xFFFFFFFF)
	.FinalXor(0x00000000)
	.ReflectIn()
	.ReflectOut()
	.Check(0x340BC6D9);

inline constexpr VUB::CrcParameters jamcrcUnchecked = VUB::CrcParameters()
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


// CRC-16/SPI-FUJITSU through the chain, with the check value and without it, with the table engine and the bitwise engine: the nine
// digits are followed by E5CC, low byte first (CC E5).
void TestSpiFujitsuThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<spiFujitsu>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<spiFujitsu, VUB::CrcBitwise>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<spiFujitsuUnchecked>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<spiFujitsuUnchecked, VUB::CrcBitwise>>({0xCC, 0xE5})));
}


// CRC-16/AUG-CCITT through the chain, with the check value, with each engine.
void TestAugCcittThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<augCcitt>>({0xCC, 0xE5})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<augCcitt, VUB::CrcBitwise>>({0xCC, 0xE5})));
}


// CRC-8/DARC through the chain: one byte, 15, with and without the check value, with each engine.
void TestDarcThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<darc>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<darc, VUB::CrcBitwise>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<darcUnchecked>>({0x15})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<darcUnchecked, VUB::CrcBitwise>>({0x15})));
}


// CRC-32/JAMCRC through the chain: 340BC6D9, so the bytes are D9 C6 0B 34, with and without the check value, with each engine.
void TestJamcrcThroughTheChain() {
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<jamcrc>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<jamcrc, VUB::CrcBitwise>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<jamcrcUnchecked>>({0xD9, 0xC6, 0x0B, 0x34})));
	SIM_EXPECT((EncodesCheckDigitsAs<VUB::Crc<jamcrcUnchecked, VUB::CrcBitwise>>({0xD9, 0xC6, 0x0B, 0x34})));
}


// The contract check of the test kit on one chained CRC: JAMCRC, unchecked, with the bitwise engine.
void TestContractOnChainedJamcrc() {
	VUB::Crc<jamcrcUnchecked, VUB::CrcBitwise> transform;
	SIM_EXPECT(VUB::CheckTransform(transform, 51, 300, 40));
}


// The check is optional: it is not part of the wire, so the fingerprint is the same with and without it. The chain of a preset gives the
// preset's fingerprint, and the defaults are the ones the wiki page gives.
void TestCheckIsOptionalAndDefaultsHold() {
	SIM_EXPECT(spiFujitsu.hasCheck);
	SIM_EXPECT(!spiFujitsuUnchecked.hasCheck);
	SIM_EXPECT(spiFujitsu.check == 0xE5CC);
	SIM_EXPECT((VUB::Crc<spiFujitsu>::fingerprint == VUB::Crc<spiFujitsuUnchecked>::fingerprint));
	SIM_EXPECT(VUB::Crc<spiFujitsuUnchecked>::checked);
	SIM_EXPECT(!spiFujitsuUnchecked.reflectIn);
	SIM_EXPECT(!spiFujitsuUnchecked.reflectOut);
	SIM_EXPECT(VUB::CrcParameters().Width(8).Polynomial(0x07).initialValue == 0);
	SIM_EXPECT(VUB::CrcParameters().Width(8).Polynomial(0x07).finalXor == 0);
	SIM_EXPECT(VUB::CrcParameters().Width(8).Polynomial(0x07).ReflectIn().reflectIn);
	SIM_EXPECT(VUB::CrcParameters().Width(8).Polynomial(0x07).ReflectOut(false).reflectOut == false);
}


void TestChainGivesThePresetFingerprint() {
	SIM_EXPECT((VUB::Crc<VUB::CrcParameters()
		.Width(16)
		.Polynomial(0x8005)
		.Initial(0xFFFF)
		.FinalXor(0x0000)
		.ReflectIn()
		.ReflectOut()
		.Check(0x4B37)>::fingerprint == VUB::Crc16Modbus::fingerprint));
}


int main() {
	TestSpiFujitsuThroughTheChain();
	TestAugCcittThroughTheChain();
	TestDarcThroughTheChain();
	TestJamcrcThroughTheChain();
	TestContractOnChainedJamcrc();
	TestCheckIsOptionalAndDefaultsHold();
	TestChainGivesThePresetFingerprint();
	return SimFinish("TestVubCrcChain");
}
