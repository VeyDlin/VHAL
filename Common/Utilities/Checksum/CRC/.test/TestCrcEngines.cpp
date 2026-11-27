#include <Utilities/Checksum/CRC/Crc.h>
#include <Utilities/Checksum/CRC/CrcParameters.h>
#include <Utilities/Checksum/CRC/Engine/CrcBitwise.h>
#include <Utilities/Checksum/CRC/Engine/CrcNibble.h>
#include <Utilities/Checksum/CRC/Engine/CrcTable.h>
#include <Utilities/DataTypes.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <span>
#include <type_traits>
#include <vector>

namespace {


// Host tests of the CRC engines. Every engine is checked against an independent bit-by-bit reference that is written here, not taken
// from the library: the reference follows the bit-serial definition of the reveng model. The reference itself is checked against the
// catalogue check values first. The engines are then compared with it over the widths 1 to 64, every combination of reflections,
// random initial values and final XORs, lengths 0 to 300, and every split of a message into parts, with empty parts. The catalogue
// values and the reference are also evaluated at compile time with the engines in static_assert.


using Checksum::Crc;
using Checksum::CrcBitwise;
using Checksum::CrcNibble;
using Checksum::CrcParameters;
using Checksum::CrcTable;


int failureCount = 0;


void Expect(bool condition, const char* expression, int line) {
	if (!condition) {
		failureCount++;
		std::fprintf(stderr, "TestCrcEngines.cpp:%d: check failed: %s\n", line, expression);
	}
}


void ExpectValue(uint64 actual, uint64 expected, const char* label, int line) {
	if (actual != expected) {
		failureCount++;
		std::fprintf(stderr, "TestCrcEngines.cpp:%d: %s is 0x%llX, expected 0x%llX\n", line, label,
			static_cast<unsigned long long>(actual), static_cast<unsigned long long>(expected));
	}
}


#define CRC_EXPECT(condition) Expect(static_cast<bool>(condition), #condition, __LINE__)
#define CRC_EQ(actual, expected) ExpectValue(static_cast<uint64>(actual), static_cast<uint64>(expected), #actual, __LINE__)


// A deterministic generator, so a failure repeats.
class TestRandom {
public:
	explicit TestRandom(uint32 seed) : state(seed) {}


	uint8 Byte() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<uint8>(state >> 7);
	}


private:
	uint32 state;
};


// The reveng model, bit by bit, with no table and no shortcut. The register of a reflected input starts reflected; the normal value
// of the register is then its reflection. The output is reflected when reflectOut is set, and the final XOR is applied last.
uint64 ReferenceReflect(uint64 value, uint8 width) {
	uint64 result = 0;
	for (uint8 bit = 0; bit < width; bit++) {
		result = (result << 1) | ((value >> bit) & 1);
	}

	return result;
}


uint64 Reference(const CrcParameters& parameters, const std::vector<uint8>& data) {
	const uint8 width = parameters.width;
	const uint64 mask = width >= 64 ? ~static_cast<uint64>(0) : (static_cast<uint64>(1) << width) - 1;
	uint64 remainder = 0;
	if (parameters.reflectIn) {
		const uint64 reflectedPolynomial = ReferenceReflect(parameters.polynomial, width);
		remainder = ReferenceReflect(parameters.initialValue & mask, width);
		for (uint8 byte : data) {
			for (uint8 bit = 0; bit < 8; bit++) {
				const uint64 input = (byte >> bit) & 1;
				const uint64 feedback = (remainder & 1) ^ input;
				remainder >>= 1;
				if (feedback != 0) {
					remainder ^= reflectedPolynomial;
				}
			}
		}

		remainder = ReferenceReflect(remainder, width);
	} else {
		remainder = parameters.initialValue & mask;
		for (uint8 byte : data) {
			for (int bit = 7; bit >= 0; bit--) {
				const uint64 input = (byte >> bit) & 1;
				const uint64 feedback = ((remainder >> (width - 1)) & 1) ^ input;
				remainder = (remainder << 1) & mask;
				if (feedback != 0) {
					remainder ^= parameters.polynomial;
				}
			}
		}
	}

	if (parameters.reflectOut) {
		remainder = ReferenceReflect(remainder, width);
	}

	return (remainder ^ parameters.finalXor) & mask;
}


const std::vector<uint8> CatalogueText = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// The catalogue entries used by the reference check: each check value is the CRC of "123456789".
struct CatalogueEntry {
	CrcParameters parameters;
	uint64 check;
	const char* name;
};


constexpr CrcParameters Make(uint32 width, uint64 polynomial, uint64 initial, uint64 finalXor, bool reflectIn, bool reflectOut) {
	return CrcParameters().Width(width).Polynomial(polynomial).Initial(initial).FinalXor(finalXor).ReflectIn(reflectIn)
		.ReflectOut(reflectOut);
}


void CheckReferenceAgainstCatalogue() {
	const std::vector<CatalogueEntry> entries = {
		{Make(3, 0x3, 0x7, 0x0, true, true), 0x6, "CRC-3/ROHC"},
		{Make(4, 0x3, 0x0, 0x0, true, true), 0x7, "CRC-4/ITU"},
		{Make(5, 0x05, 0x1F, 0x1F, true, true), 0x19, "CRC-5/USB"},
		{Make(7, 0x09, 0x00, 0x00, false, false), 0x75, "CRC-7/MMC"},
		{Make(8, 0x07, 0x00, 0x00, false, false), 0xF4, "CRC-8/SMBUS"},
		{Make(16, 0x8005, 0x0000, 0x0000, true, true), 0xBB3D, "CRC-16/ARC"},
		{Make(16, 0x1021, 0x0000, 0x0000, false, false), 0x31C3, "CRC-16/XMODEM"},
		{Make(16, 0x8005, 0xFFFF, 0x0000, true, true), 0x4B37, "CRC-16/MODBUS"},
		{Make(16, 0x1021, 0xC6C6, 0x0000, true, true), 0xBF05, "CRC-A"},
		{Make(16, 0x1021, 0xB2AA, 0x0000, true, true), 0x63D0, "RIELLO"},
		{Make(32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true), 0xCBF43926, "CRC-32/ISO-HDLC"},
		{Make(32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, false, false), 0xFC891918, "CRC-32/BZIP2"},
		{Make(32, 0x1EDC6F41, 0xFFFFFFFF, 0xFFFFFFFF, true, true), 0xE3069283, "CRC-32C"},
		{Make(64, 0x42F0E1EBA9EA3693, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, true, true), 0x995DC9BBDF1939FA, "CRC-64/XZ"},
		{Make(64, 0x42F0E1EBA9EA3693, 0x0, 0x0, false, false), 0x6C40DF5F0B497347, "CRC-64/ECMA-182"},
	};

	for (const CatalogueEntry& entry : entries) {
		ExpectValue(Reference(entry.parameters, CatalogueText), entry.check, entry.name, __LINE__);
	}
}


// Splits the message at every point and at the ends, with empty parts between, and compares with the one-call value.
template<typename CrcType>
void CheckParts(const std::vector<uint8>& data, uint64 expected, int line) {
	const std::size_t size = data.size();
	for (std::size_t split = 0; split <= size; split++) {
		CrcType crc;
		crc.Add(std::span<const uint8>(data.data(), split));
		crc.Add(std::span<const uint8>());
		crc.Add(std::span<const uint8>(data.data() + split, size - split));
		if (static_cast<uint64>(crc.Get()) != expected) {
			failureCount++;
			std::fprintf(stderr, "TestCrcEngines.cpp:%d: split of %zu bytes at %zu gives 0x%llX, expected 0x%llX\n", line, size,
				split, static_cast<unsigned long long>(crc.Get()), static_cast<unsigned long long>(expected));
			return;
		}
	}
}


template<typename CrcType>
void CheckEngine(const CrcParameters& parameters, TestRandom& random, int line) {
	CrcType crc;
	crc.Reset();
	for (std::size_t length = 0; length <= 300; length++) {
		std::vector<uint8> data(length);
		for (std::size_t index = 0; index < length; index++) {
			data[index] = random.Byte();
		}

		const uint64 expected = Reference(parameters, data);
		ExpectValue(static_cast<uint64>(CrcType::Calculate(data)), expected, "Calculate", line);
		if (length <= 24 || length % 37 == 0) {
			CheckParts<CrcType>(data, expected, line);
		}
	}
}


// The parameters of one case: the engines the width supports are run; the others are checked not to support it.
template<CrcParameters Parameters>
void CheckCase(uint32 seed, int line) {
	TestRandom random(seed);
	CheckEngine<Crc<Parameters, CrcBitwise>>(Parameters, random, line);
	if constexpr (CrcNibble::Supports<Parameters>()) {
		CheckEngine<Crc<Parameters, CrcNibble>>(Parameters, random, line);
	}

	if constexpr (CrcTable::Supports<Parameters>()) {
		CheckEngine<Crc<Parameters, CrcTable>>(Parameters, random, line);
	}
}


// The catalogue values at compile time, through each engine that supports the width.
constexpr std::array<uint8, 9> CompileText = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};
constexpr CrcParameters CrcA = Make(16, 0x1021, 0xC6C6, 0x0000, true, true);
constexpr CrcParameters Riello = Make(16, 0x1021, 0xB2AA, 0x0000, true, true);
constexpr CrcParameters Modbus = Make(16, 0x8005, 0xFFFF, 0x0000, true, true);

static_assert(Crc<CrcA, CrcBitwise>::Calculate(CompileText) == 0xBF05);
static_assert(Crc<CrcA, CrcNibble>::Calculate(CompileText) == 0xBF05);
static_assert(Crc<CrcA, CrcTable>::Calculate(CompileText) == 0xBF05);
static_assert(Crc<Riello, CrcBitwise>::Calculate(CompileText) == 0x63D0);
static_assert(Crc<Riello, CrcNibble>::Calculate(CompileText) == 0x63D0);
static_assert(Crc<Riello, CrcTable>::Calculate(CompileText) == 0x63D0);
static_assert(Crc<Modbus, CrcBitwise>::Calculate(CompileText) == 0x4B37);
static_assert(Crc<Modbus, CrcTable>::Calculate(CompileText) == 0x4B37);
static_assert(Crc<Modbus, CrcBitwise>::With<CrcNibble>::Calculate(CompileText) == 0x4B37);
static_assert(std::is_same_v<Crc<Modbus>::Value, uint16>);
static_assert(std::is_same_v<Crc<Make(3, 0x3, 0, 0, true, true)>::Value, uint8>);
static_assert(std::is_same_v<Crc<Make(24, 0x800063, 0, 0, false, false)>::Value, uint32>);
static_assert(std::is_same_v<Crc<Make(64, 0x42F0E1EBA9EA3693, 0, 0, false, false)>::Value, uint64>);
static_assert(!CrcTable::Supports<Make(4, 0x3, 0, 0, false, false)>());
static_assert(CrcNibble::Supports<Make(4, 0x3, 0, 0, false, false)>());
static_assert(CrcBitwise::Supports<Make(1, 0x1, 0, 0, false, false)>());


// A parts test at compile time: two parts with an empty one between them.
constexpr uint64 CompileParts() {
	Crc<CrcA, CrcTable> crc;
	crc.Add(std::span<const uint8>(CompileText.data(), 4));
	crc.Add(std::span<const uint8>());
	crc.Add(std::span<const uint8>(CompileText.data() + 4, 5));
	return crc.Get();
}


static_assert(CompileParts() == 0xBF05);


void CheckCompileRegister() {
	// The CRC of no bytes is the reflected initial value 0xC6C6, since the input and the output are both reflected: 0x6363.
	Crc<CrcA, CrcBitwise> crc;
	crc.Add(CompileText);
	CRC_EQ(crc.Get(), 0xBF05);
	crc.Reset();
	CRC_EQ(crc.Get(), 0x6363);
	crc.Add(std::span<const uint8>(CompileText.data(), 9));
	CRC_EQ(crc.Get(), 0xBF05);
}


// The cases: every width of the list, with each reflection, and a random initial value and final XOR (the constants are masked to
// the width by the library, so the high bits of the constants are part of the case).
template<uint8 Width, uint64 Polynomial, uint64 Initial, uint64 FinalXor>
void CheckWidth(uint32 seed, int line) {
	CheckCase<Make(Width, Polynomial, Initial, FinalXor, false, false)>(seed, line);
	CheckCase<Make(Width, Polynomial, Initial, FinalXor, true, false)>(seed + 1, line);
	CheckCase<Make(Width, Polynomial, Initial, FinalXor, false, true)>(seed + 2, line);
	CheckCase<Make(Width, Polynomial, Initial, FinalXor, true, true)>(seed + 3, line);
}


void CheckAllWidths() {
	CheckWidth<1, 0x1, 0x1, 0x0>(0x0F01, __LINE__);
	CheckWidth<2, 0x3, 0x2, 0x1>(0x0F02, __LINE__);
	CheckWidth<3, 0x5, 0x6, 0x1>(0x1001, __LINE__);
	CheckWidth<4, 0x9, 0xA, 0x7>(0x1002, __LINE__);
	CheckWidth<5, 0x15, 0x1C, 0x0B>(0x1003, __LINE__);
	CheckWidth<7, 0x43, 0x2F, 0x55>(0x1004, __LINE__);
	CheckWidth<8, 0xA7, 0x3C, 0x9E>(0x1005, __LINE__);
	CheckWidth<10, 0x233, 0x1A4, 0x0C5>(0x1006, __LINE__);
	CheckWidth<12, 0x80F, 0x5A3, 0x6E1>(0x1007, __LINE__);
	CheckWidth<15, 0x4599, 0x2C1B, 0x3F30>(0x1008, __LINE__);
	CheckWidth<16, 0x8005, 0x5F3A, 0xA6C1>(0x1009, __LINE__);
	CheckWidth<17, 0x1A0F3, 0x1F0B5, 0x0ED31>(0x100A, __LINE__);
	CheckWidth<21, 0x102899, 0x0C3F5A, 0x1B2C4D>(0x100B, __LINE__);
	CheckWidth<24, 0x864CFB, 0xB704CE, 0x5A5A5A>(0x100C, __LINE__);
	CheckWidth<30, 0x2030B9C7, 0x1A2B3C4D, 0x0F0F0F0F>(0x100D, __LINE__);
	CheckWidth<31, 0x02608EDB, 0x7EADBEEF, 0x3C2B1A09>(0x100E, __LINE__);
	CheckWidth<32, 0x04C11DB7, 0x9A3B5C7D, 0x1F2E3D4C>(0x100F, __LINE__);
	CheckWidth<40, 0x0004820009, 0x12C4D9E3F5, 0x7A6B5C4D3E>(0x1010, __LINE__);
	CheckWidth<64, 0x42F0E1EBA9EA3693, 0x0123456789ABCDEF, 0xFEDCBA9876543210>(0x1011, __LINE__);
}


void CheckCatalogueEngines() {
	CRC_EXPECT(CrcBitwise::Supports<CrcA>());
	CRC_EXPECT(CrcNibble::Supports<CrcA>());
	CRC_EXPECT(CrcTable::Supports<CrcA>());
	CheckCase<CrcA>(0x2001, __LINE__);
	CheckCase<Riello>(0x2002, __LINE__);
	CheckCase<Modbus>(0x2003, __LINE__);
	CheckCase<Make(32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true)>(0x2004, __LINE__);
	CheckCase<Make(64, 0x42F0E1EBA9EA3693, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, true, true)>(0x2005, __LINE__);
	CheckCase<Make(64, 0x42F0E1EBA9EA3693, 0x0, 0x0, false, false)>(0x2006, __LINE__);
	CheckCase<Make(5, 0x05, 0x1F, 0x1F, true, true)>(0x2007, __LINE__);
}


constexpr CrcParameters Iso32 = Make(32, 0x04C11DB7, 0xFFFFFFFF, 0xFFFFFFFF, true, true);
constexpr CrcParameters Xz64 = Make(64, 0x42F0E1EBA9EA3693, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, true, true);


void CheckCatalogueValues() {
	using CrcTableOf32 = Crc<Iso32, CrcTable>;
	using CrcNibbleOf16 = Crc<Modbus, CrcNibble>;
	using CrcXz = Crc<Xz64>;
	CRC_EQ(Crc<CrcA>::Calculate(CatalogueText), 0xBF05);
	CRC_EQ(Crc<Riello>::Calculate(CatalogueText), 0x63D0);
	CRC_EQ(CrcNibbleOf16::Calculate(CatalogueText), 0x4B37);
	CRC_EQ(CrcTableOf32::Calculate(CatalogueText), 0xCBF43926);
	CRC_EQ(CrcXz::Calculate(CatalogueText), 0x995DC9BBDF1939FA);
}
}


int main() {
	CheckReferenceAgainstCatalogue();
	CheckCompileRegister();
	CheckCatalogueValues();
	CheckCatalogueEngines();
	CheckAllWidths();

	if (failureCount != 0) {
		std::fprintf(stderr, "%d CRC check(s) failed\n", failureCount);
		return 1;
	}

	std::printf("all CRC engine checks passed\n");
	return 0;
}
