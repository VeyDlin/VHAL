#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/Engine/CrcBitwise.h>
#include <Utilities/Checksum/CRC/Engine/CrcNibble.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/Checksum/CRC/Preset/Crc4Itu.h>
#include <Utilities/Checksum/CRC/Preset/Crc5Epc.h>
#include <Utilities/Checksum/CRC/Preset/Crc5Itu.h>
#include <Utilities/Checksum/CRC/Preset/Crc5Usb.h>
#include <Utilities/Checksum/CRC/Preset/Crc6Cdma2000A.h>
#include <Utilities/Checksum/CRC/Preset/Crc6Cdma2000B.h>
#include <Utilities/Checksum/CRC/Preset/Crc6Itu.h>
#include <Utilities/Checksum/CRC/Preset/Crc7Mmc.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Ebu.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Maxim.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Smbus.h>
#include <Utilities/Checksum/CRC/Preset/Crc8Wcdma.h>
#include <Utilities/Checksum/CRC/Preset/Crc10.h>
#include <Utilities/Checksum/CRC/Preset/Crc10Cdma2000.h>
#include <Utilities/Checksum/CRC/Preset/Crc11.h>
#include <Utilities/Checksum/CRC/Preset/Crc12Cdma2000.h>
#include <Utilities/Checksum/CRC/Preset/Crc12Dect.h>
#include <Utilities/Checksum/CRC/Preset/Crc12Umts.h>
#include <Utilities/Checksum/CRC/Preset/Crc13Bbc.h>
#include <Utilities/Checksum/CRC/Preset/Crc15Can.h>
#include <Utilities/Checksum/CRC/Preset/Crc15Mpt1327.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Arc.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Buypass.h>
#include <Utilities/Checksum/CRC/Preset/Crc16CcittFalse.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Cdma2000.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Cms.h>
#include <Utilities/Checksum/CRC/Preset/Crc16DectR.h>
#include <Utilities/Checksum/CRC/Preset/Crc16DectX.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Dnp.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Genibus.h>
#include <Utilities/Checksum/CRC/Preset/Crc16IsoIec144433A.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Kermit.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Maxim.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Modbus.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Riello.h>
#include <Utilities/Checksum/CRC/Preset/Crc16T10Dif.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Tms37157.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Usb.h>
#include <Utilities/Checksum/CRC/Preset/Crc16X25.h>
#include <Utilities/Checksum/CRC/Preset/Crc16Xmodem.h>
#include <Utilities/Checksum/CRC/Preset/Crc17Can.h>
#include <Utilities/Checksum/CRC/Preset/Crc21Can.h>
#include <Utilities/Checksum/CRC/Preset/Crc24FlexrayA.h>
#include <Utilities/Checksum/CRC/Preset/Crc24FlexrayB.h>
#include <Utilities/Checksum/CRC/Preset/Crc24OpenPgp.h>
#include <Utilities/Checksum/CRC/Preset/Crc30Cdma.h>
#include <Utilities/Checksum/CRC/Preset/Crc32Bzip2.h>
#include <Utilities/Checksum/CRC/Preset/Crc32C.h>
#include <Utilities/Checksum/CRC/Preset/Crc32IsoHdlc.h>
#include <Utilities/Checksum/CRC/Preset/Crc32Mpeg2.h>
#include <Utilities/Checksum/CRC/Preset/Crc32Posix.h>
#include <Utilities/Checksum/CRC/Preset/Crc32Q.h>
#include <Utilities/Checksum/CRC/Preset/Crc40Gsm.h>
#include <Utilities/Checksum/CRC/Preset/Crc64Ecma182.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstdio>
#include <random>
#include <span>
#include <vector>

namespace {


using Checksum::CrcBitwise;
using Checksum::CrcNibble;
using Checksum::CrcParameters;
using Checksum::CrcTable;


// The text "123456789", that every check value is the CRC of.
constexpr std::array<uint8, 9> CheckText = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// Reverses the low bits bits of value, the least significant bit becoming the most significant of the result.
uint64 ReverseBits(uint64 value, uint8 bits) {
	uint64 result = 0;
	for (uint8 bit = 0; bit < bits; bit++) {
		result = (result << 1) | ((value >> bit) & 1);
	}

	return result;
}


// Independent reference, written apart from the library: the reveng model, one bit at a time, with the register unreflected. The input
// is reflected bit by bit when ReflectIn is set; the output register is reflected before the final XOR when ReflectOut is set.
uint64 ReferenceCrc(const CrcParameters& parameters, std::span<const uint8> data) {
	const uint8 width = parameters.width;
	const uint64 mask = width >= 64 ? ~static_cast<uint64>(0) : (static_cast<uint64>(1) << width) - 1;
	const uint64 top = static_cast<uint64>(1) << (width - 1);
	uint64 reg = parameters.initialValue & mask;

	for (uint8 byte : data) {
		for (uint8 bit = 0; bit < 8; bit++) {
			const uint64 in = parameters.reflectIn ? (byte >> bit) & 1 : (byte >> (7 - bit)) & 1;
			const bool feedback = ((reg & top) != 0) != (in != 0);
			reg = (reg << 1) & mask;
			if (feedback) {
				reg ^= parameters.polynomial & mask;
			}
		}
	}

	if (parameters.reflectOut) {
		reg = ReverseBits(reg, width);
	}

	return (reg ^ parameters.finalXor) & mask;
}


std::mt19937 generator(20261125);
int failures = 0;


// Checks one preset with one engine: the check value, then random messages against the reference, each one whole and in two parts.
template<typename Preset, typename Engine>
void CheckEngine(const char* presetName, const char* engineName) {
	if constexpr (Engine::template Supports<Preset::parameters>()) {
		using Checked = typename Preset::template With<Engine>;

		static_assert(Checked::checked);

		const uint64 checkValue = static_cast<uint64>(Checked::Calculate(std::span<const uint8>(CheckText)));
		if (checkValue != Preset::parameters.check) {
			failures++;
			std::printf("CHECK %s with %s: %llX, the check value is %llX\n", presetName, engineName,
				static_cast<unsigned long long>(checkValue), static_cast<unsigned long long>(Preset::parameters.check));
		}

		int wrong = 0;
		for (int round = 0; round < 200; round++) {
			std::vector<uint8> data(generator() % 101);
			for (uint8& byte : data) {
				byte = static_cast<uint8>(generator());
			}

			const uint64 expected = ReferenceCrc(Preset::parameters, data);
			const std::size_t cut = data.empty() ? 0 : generator() % (data.size() + 1);

			Checked whole;
			whole.Add(std::span<const uint8>(data));
			wrong += static_cast<uint64>(whole.Get()) != expected ? 1 : 0;

			Checked parts;
			parts.Add(std::span<const uint8>(data.data(), cut));
			parts.Add(std::span<const uint8>(data.data() + cut, data.size() - cut));
			wrong += static_cast<uint64>(parts.Get()) != expected ? 1 : 0;

			parts.Reset();
			parts.Add(std::span<const uint8>(data));
			wrong += static_cast<uint64>(parts.Get()) != expected ? 1 : 0;
		}

		if (wrong != 0) {
			failures++;
			std::printf("RANDOM %s with %s: %d of 600 results differ from the reference\n", presetName, engineName, wrong);
		}
	}
}


// Checks one preset with every engine that supports it.
template<typename Preset>
void CheckPreset(const char* presetName) {
	static_assert(Preset::checked);

	CheckEngine<Preset, CrcBitwise>(presetName, "CrcBitwise");
	CheckEngine<Preset, CrcNibble>(presetName, "CrcNibble");
	CheckEngine<Preset, CrcTable>(presetName, "CrcTable");
}


// The check value of a CRC type, computed at compile time with the type's engine.
template<typename Checked>
constexpr bool ChecksAtCompileTime() {
	return Checked::Calculate(std::span<const uint8>(CheckText)) == Checked::parameters.check;
}


static_assert(ChecksAtCompileTime<Checksum::Crc16Modbus>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Modbus::With<CrcBitwise>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Modbus::With<CrcNibble>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Modbus::With<CrcTable>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Riello>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Riello::With<CrcBitwise>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Riello::With<CrcNibble>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16Riello::With<CrcTable>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16IsoIec144433A>());
static_assert(ChecksAtCompileTime<Checksum::Crc16IsoIec144433A::With<CrcBitwise>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16IsoIec144433A::With<CrcNibble>>());
static_assert(ChecksAtCompileTime<Checksum::Crc16IsoIec144433A::With<CrcTable>>());
}


int main() {
	CheckPreset<Checksum::Crc4Itu>("Crc4Itu");
	CheckPreset<Checksum::Crc5Epc>("Crc5Epc");
	CheckPreset<Checksum::Crc5Itu>("Crc5Itu");
	CheckPreset<Checksum::Crc5Usb>("Crc5Usb");
	CheckPreset<Checksum::Crc6Cdma2000A>("Crc6Cdma2000A");
	CheckPreset<Checksum::Crc6Cdma2000B>("Crc6Cdma2000B");
	CheckPreset<Checksum::Crc6Itu>("Crc6Itu");
	CheckPreset<Checksum::Crc7Mmc>("Crc7Mmc");
	CheckPreset<Checksum::Crc8Smbus>("Crc8Smbus");
	CheckPreset<Checksum::Crc8Ebu>("Crc8Ebu");
	CheckPreset<Checksum::Crc8Maxim>("Crc8Maxim");
	CheckPreset<Checksum::Crc8Wcdma>("Crc8Wcdma");
	CheckPreset<Checksum::Crc10>("Crc10");
	CheckPreset<Checksum::Crc10Cdma2000>("Crc10Cdma2000");
	CheckPreset<Checksum::Crc11>("Crc11");
	CheckPreset<Checksum::Crc12Cdma2000>("Crc12Cdma2000");
	CheckPreset<Checksum::Crc12Dect>("Crc12Dect");
	CheckPreset<Checksum::Crc12Umts>("Crc12Umts");
	CheckPreset<Checksum::Crc13Bbc>("Crc13Bbc");
	CheckPreset<Checksum::Crc15Can>("Crc15Can");
	CheckPreset<Checksum::Crc15Mpt1327>("Crc15Mpt1327");
	CheckPreset<Checksum::Crc16Arc>("Crc16Arc");
	CheckPreset<Checksum::Crc16Buypass>("Crc16Buypass");
	CheckPreset<Checksum::Crc16CcittFalse>("Crc16CcittFalse");
	CheckPreset<Checksum::Crc16Cdma2000>("Crc16Cdma2000");
	CheckPreset<Checksum::Crc16Cms>("Crc16Cms");
	CheckPreset<Checksum::Crc16DectR>("Crc16DectR");
	CheckPreset<Checksum::Crc16DectX>("Crc16DectX");
	CheckPreset<Checksum::Crc16Dnp>("Crc16Dnp");
	CheckPreset<Checksum::Crc16Genibus>("Crc16Genibus");
	CheckPreset<Checksum::Crc16IsoIec144433A>("Crc16IsoIec144433A");
	CheckPreset<Checksum::Crc16Kermit>("Crc16Kermit");
	CheckPreset<Checksum::Crc16Maxim>("Crc16Maxim");
	CheckPreset<Checksum::Crc16Modbus>("Crc16Modbus");
	CheckPreset<Checksum::Crc16Riello>("Crc16Riello");
	CheckPreset<Checksum::Crc16T10Dif>("Crc16T10Dif");
	CheckPreset<Checksum::Crc16Tms37157>("Crc16Tms37157");
	CheckPreset<Checksum::Crc16Usb>("Crc16Usb");
	CheckPreset<Checksum::Crc16X25>("Crc16X25");
	CheckPreset<Checksum::Crc16Xmodem>("Crc16Xmodem");
	CheckPreset<Checksum::Crc17Can>("Crc17Can");
	CheckPreset<Checksum::Crc21Can>("Crc21Can");
	CheckPreset<Checksum::Crc24OpenPgp>("Crc24OpenPgp");
	CheckPreset<Checksum::Crc24FlexrayA>("Crc24FlexrayA");
	CheckPreset<Checksum::Crc24FlexrayB>("Crc24FlexrayB");
	CheckPreset<Checksum::Crc30Cdma>("Crc30Cdma");
	CheckPreset<Checksum::Crc32IsoHdlc>("Crc32IsoHdlc");
	CheckPreset<Checksum::Crc32Bzip2>("Crc32Bzip2");
	CheckPreset<Checksum::Crc32C>("Crc32C");
	CheckPreset<Checksum::Crc32Mpeg2>("Crc32Mpeg2");
	CheckPreset<Checksum::Crc32Posix>("Crc32Posix");
	CheckPreset<Checksum::Crc32Q>("Crc32Q");
	CheckPreset<Checksum::Crc40Gsm>("Crc40Gsm");
	CheckPreset<Checksum::Crc64Ecma182>("Crc64Ecma182");

	if (failures != 0) {
		std::printf("%d preset check(s) failed\n", failures);
		return 1;
	}

	std::printf("all 54 presets checked with every engine that supports them\n");
	return 0;
}
