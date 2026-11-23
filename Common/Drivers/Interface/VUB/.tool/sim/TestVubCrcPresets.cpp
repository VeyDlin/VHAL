#include "SimCheck.h"
#include <Drivers/Interface/VUB/Engine/VubCrcBitwise.h>
#include <Drivers/Interface/VUB/Engine/VubCrcCore.h>
#include <Drivers/Interface/VUB/Engine/VubCrcTable.h>
#include <Drivers/Interface/VUB/TestKit/VubRandom.h>
#include <Drivers/Interface/VUB/Transform/VubCrc.h>
#include <Drivers/Interface/VUB/VubBlocks.h>
#include <Drivers/Interface/VUB/VubMeta.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <array>
#include <cstddef>
#include <span>
#include <type_traits>


// The catalogue check string, "123456789", as bytes. Every preset's check value is the CRC of these nine bytes.
inline constexpr std::array<uint8, 9> checkDigits = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39};


// A preset's check value, by both engines, at compile time.
template<typename Crc>
constexpr bool PresetChecksAtCompileTime() {
	using Algorithm = VubCrcAlgorithm<Crc::parameters>;
	std::span<const uint8> digits(checkDigits.data(), checkDigits.size());
	return VubCrcTable::Run<Algorithm>(digits) == Crc::parameters.check && VubCrcBitwise::Run<Algorithm>(digits) == Crc::parameters.check;
}


// The same at run time, with the digits in a buffer that is not a constant.
template<typename Crc>
bool PresetChecksAtRunTime() {
	using Algorithm = VubCrcAlgorithm<Crc::parameters>;
	std::array<uint8, 9> digits = checkDigits;
	std::span<const uint8> bytes(digits.data(), digits.size());
	bool tableOk = VubCrcTable::Run<Algorithm>(bytes) == Crc::parameters.check;
	bool bitwiseOk = VubCrcBitwise::Run<Algorithm>(bytes) == Crc::parameters.check;
	return tableOk && bitwiseOk;
}


// The table engine and the bitwise engine agree on random data of 0 to 64 bytes, for one preset.
template<typename Crc>
bool EnginesAgree(uint32 seed) {
	using Algorithm = VubCrcAlgorithm<Crc::parameters>;
	VubRandom random(seed);
	std::array<uint8, 64> data{};
	for (std::size_t round = 0; round < 300; round++) {
		std::size_t length = random.Below(65);
		for (std::size_t index = 0; index < length; index++) {
			data[index] = static_cast<uint8>(random.Next());
		}

		std::span<const uint8> bytes(data.data(), length);
		if (VubCrcTable::Run<Algorithm>(bytes) != VubCrcBitwise::Run<Algorithm>(bytes)) {
			return false;
		}
	}

	return true;
}


// The CRC of an empty message is the initial value passed through the final step: reflected when reflectOut is set, then the final
// XOR. Both engines must give the value that is worked out by hand.
template<typename Crc>
bool EmptyMessageGives(uint32 expected) {
	using Algorithm = VubCrcAlgorithm<Crc::parameters>;
	std::span<const uint8> empty;
	return VubCrcTable::Run<Algorithm>(empty) == expected && VubCrcBitwise::Run<Algorithm>(empty) == expected;
}


// The CRC of the reveng model, written from its definition and bit by bit, with no table and no code shared with the engines. The
// register starts at the initial value as an unreflected register. Each input byte is reflected first when reflectIn is set. The
// register shifts MSB first against the polynomial in normal form. The register is reflected when reflectOut is set, and the final XOR
// is applied last.
constexpr uint32 ReferenceCrc(const VubCrcParameters& parameters, std::span<const uint8> data) {
	const uint32 mask = parameters.width == 32 ? 0xFFFFFFFFu : ((1u << parameters.width) - 1u);
	const uint32 top = 1u << (parameters.width - 1);
	uint32 reg = parameters.initialValue & mask;
	for (uint8 byte : data) {
		uint32 value = byte;
		if (parameters.reflectIn) {
			uint32 reversed = 0;
			for (uint32 bit = 0; bit < 8; bit++) {
				reversed = (reversed << 1) | ((value >> bit) & 1u);
			}

			value = reversed;
		}

		reg ^= value << (parameters.width - 8);
		for (uint32 bit = 0; bit < 8; bit++) {
			reg = (reg & top) != 0 ? ((reg << 1) ^ parameters.polynomial) & mask : (reg << 1) & mask;
		}
	}

	if (parameters.reflectOut) {
		uint32 reversed = 0;
		for (uint32 bit = 0; bit < parameters.width; bit++) {
			reversed = (reversed << 1) | ((reg >> bit) & 1u);
		}

		reg = reversed;
	}

	return (reg ^ parameters.finalXor) & mask;
}


// Parameter sets written here, not taken from the presets, so the reference is checked against the catalogue on its own.
inline constexpr VubCrcParameters modbusParameters = {16, 0x8005, 0xFFFF, 0x0000, true, true, 0x4B37};
inline constexpr VubCrcParameters tmsParameters = {16, 0x1021, 0x89EC, 0x0000, true, true, 0x26B1};
inline constexpr VubCrcParameters rielloParameters = {16, 0x1021, 0xB2AA, 0x0000, true, true, 0x63D0};


// The reference gives the catalogue check values for three sets; the two last have a non-symmetric initial value.
static_assert(ReferenceCrc(modbusParameters, std::span<const uint8>(checkDigits.data(), checkDigits.size())) == 0x4B37);
static_assert(ReferenceCrc(tmsParameters, std::span<const uint8>(checkDigits.data(), checkDigits.size())) == 0x26B1);
static_assert(ReferenceCrc(rielloParameters, std::span<const uint8>(checkDigits.data(), checkDigits.size())) == 0x63D0);


// A parameter set that is not in the catalogue: reflectIn differs from reflectOut, and the initial value is not symmetric. Its check value
// is the reference's CRC of the digits, 4574.
inline constexpr VubCrcParameters customParameters = {16, 0x1021, 0x1D0F, 0x00FF, true, false, 0x4574};
using CustomCrc = VubCrc<customParameters>;
static_assert(ReferenceCrc(customParameters, std::span<const uint8>(checkDigits.data(), checkDigits.size())) == 0x4574);


// The compile-time checks: every preset matches its catalogue check value with both engines.
static_assert(PresetChecksAtCompileTime<VubCrc8Smbus>());
static_assert(PresetChecksAtCompileTime<VubCrc8Ebu>());
static_assert(PresetChecksAtCompileTime<VubCrc8Maxim>());
static_assert(PresetChecksAtCompileTime<VubCrc8Wcdma>());
static_assert(PresetChecksAtCompileTime<VubCrc16Arc>());
static_assert(PresetChecksAtCompileTime<VubCrc16Buypass>());
static_assert(PresetChecksAtCompileTime<VubCrc16CcittFalse>());
static_assert(PresetChecksAtCompileTime<VubCrc16Cdma2000>());
static_assert(PresetChecksAtCompileTime<VubCrc16Cms>());
static_assert(PresetChecksAtCompileTime<VubCrc16DectR>());
static_assert(PresetChecksAtCompileTime<VubCrc16DectX>());
static_assert(PresetChecksAtCompileTime<VubCrc16Dnp>());
static_assert(PresetChecksAtCompileTime<VubCrc16Genibus>());
static_assert(PresetChecksAtCompileTime<VubCrc16Kermit>());
static_assert(PresetChecksAtCompileTime<VubCrc16Maxim>());
static_assert(PresetChecksAtCompileTime<VubCrc16Modbus>());
static_assert(PresetChecksAtCompileTime<VubCrc16Riello>());
static_assert(PresetChecksAtCompileTime<VubCrc16T10Dif>());
static_assert(PresetChecksAtCompileTime<VubCrc16Tms37157>());
static_assert(PresetChecksAtCompileTime<VubCrc16Usb>());
static_assert(PresetChecksAtCompileTime<CustomCrc>());
static_assert(PresetChecksAtCompileTime<VubCrc16X25>());
static_assert(PresetChecksAtCompileTime<VubCrc16Xmodem>());
static_assert(PresetChecksAtCompileTime<VubCrc32IsoHdlc>());
static_assert(PresetChecksAtCompileTime<VubCrc32Bzip2>());
static_assert(PresetChecksAtCompileTime<VubCrc32C>());
static_assert(PresetChecksAtCompileTime<VubCrc32Mpeg2>());
static_assert(PresetChecksAtCompileTime<VubCrc32Posix>());
static_assert(PresetChecksAtCompileTime<VubCrc32Q>());


// A test engine that computes only CRC-16/MODBUS (width 16, polynomial 8005, initial FFFF, no final XOR, reflected in and out). It
// shows the builder's check of an engine's Supports.
struct ModbusOnlyEngine {
	static constexpr bool synchronous = true;


	template<typename Algorithm>
	static constexpr bool Supports() {
		const VubCrcParameters& parameters = Algorithm::parameters;
		return parameters.width == 16 && parameters.polynomial == 0x8005 && parameters.initialValue == 0xFFFF && parameters.finalXor == 0 &&
			parameters.reflectIn && parameters.reflectOut;
	}


	template<typename Algorithm>
	static constexpr uint32 Run(std::span<const uint8> data) {
		return VubCrcTable::Run<Algorithm>(data);
	}
};


static_assert(VubEngine<ModbusOnlyEngine>);
static_assert(VubEngineFor<ModbusOnlyEngine, VubCrcAlgorithm<VubCrc16Modbus::parameters>>);
static_assert(!VubEngineFor<ModbusOnlyEngine, VubCrcAlgorithm<VubCrc16Usb::parameters>>);
static_assert(!VubEngineFor<ModbusOnlyEngine, VubCrcAlgorithm<VubCrc8Smbus::parameters>>);
static_assert(VubEngineFor<VubCrcTable, VubCrcAlgorithm<VubCrc32IsoHdlc::parameters>>);
static_assert(VubEngineFor<VubCrcBitwise, VubCrcAlgorithm<VubCrc8Smbus::parameters>>);


// The fingerprint of a CRC does not depend on the engine: the same wire, the same fingerprint. It does depend on the byte order, which is
// on the wire, and on every parameter, the initial value included.
static_assert(VubCrc16Modbus::fingerprint == VubCrc<VubCrc16Modbus::parameters, VubCrcBitwise>::fingerprint);
static_assert(VubCrc16Modbus::fingerprint != VubCrc16Usb::fingerprint);
static_assert(VubCrc16Modbus::fingerprint != VubCrc16ModbusHighByteFirst::fingerprint);
static_assert(VubCrcHighByteFirst<VubCrc16Modbus::parameters, VubCrcBitwise>::fingerprint == VubCrc16ModbusHighByteFirst::fingerprint);
static_assert(VubCrc16Tms37157::fingerprint != VubCrc16Riello::fingerprint);
static_assert(CustomCrc::fingerprint != VubCrcHighByteFirst<customParameters>::fingerprint);


// The check values of two presets, each recomputed by the reference model from its parameters, and compared with the stored value. The
// two share the polynomial 8005; the initial value and the final XOR tell them apart. A wrong check value is a compile error: that is
// covered by BuilderErrors/crc_check_value_mismatch.cpp, not by this test.
void TestPresetCheckValuesMatchReference() {
	std::span<const uint8> digits(checkDigits.data(), checkDigits.size());
	SIM_EXPECT(ReferenceCrc(VubCrc16Modbus::parameters, digits) == VubCrc16Modbus::parameters.check);
	SIM_EXPECT(ReferenceCrc(VubCrc16Usb::parameters, digits) == VubCrc16Usb::parameters.check);
}


void TestPresetsAtRunTime() {
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc8Smbus>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc8Ebu>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc8Maxim>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc8Wcdma>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Arc>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Buypass>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16CcittFalse>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Cdma2000>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Cms>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16DectR>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16DectX>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Dnp>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Genibus>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Kermit>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Maxim>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Modbus>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Riello>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16T10Dif>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Tms37157>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Usb>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16X25>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc16Xmodem>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32IsoHdlc>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32Bzip2>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32C>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32Mpeg2>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32Posix>());
	SIM_EXPECT(PresetChecksAtRunTime<VubCrc32Q>());
}


void TestEnginesAgreeOnRandomData() {
	SIM_EXPECT(EnginesAgree<VubCrc8Smbus>(1));
	SIM_EXPECT(EnginesAgree<VubCrc8Ebu>(2));
	SIM_EXPECT(EnginesAgree<VubCrc8Maxim>(3));
	SIM_EXPECT(EnginesAgree<VubCrc8Wcdma>(4));
	SIM_EXPECT(EnginesAgree<VubCrc16Arc>(5));
	SIM_EXPECT(EnginesAgree<VubCrc16Buypass>(6));
	SIM_EXPECT(EnginesAgree<VubCrc16CcittFalse>(7));
	SIM_EXPECT(EnginesAgree<VubCrc16Cdma2000>(8));
	SIM_EXPECT(EnginesAgree<VubCrc16Cms>(9));
	SIM_EXPECT(EnginesAgree<VubCrc16DectR>(10));
	SIM_EXPECT(EnginesAgree<VubCrc16DectX>(11));
	SIM_EXPECT(EnginesAgree<VubCrc16Dnp>(12));
	SIM_EXPECT(EnginesAgree<VubCrc16Genibus>(13));
	SIM_EXPECT(EnginesAgree<VubCrc16Kermit>(14));
	SIM_EXPECT(EnginesAgree<VubCrc16Maxim>(15));
	SIM_EXPECT(EnginesAgree<VubCrc16Modbus>(16));
	SIM_EXPECT(EnginesAgree<VubCrc16T10Dif>(17));
	SIM_EXPECT(EnginesAgree<VubCrc16Usb>(18));
	SIM_EXPECT(EnginesAgree<VubCrc16Riello>(27));
	SIM_EXPECT(EnginesAgree<VubCrc16Tms37157>(28));
	SIM_EXPECT(EnginesAgree<CustomCrc>(29));
	SIM_EXPECT(EnginesAgree<VubCrc16X25>(19));
	SIM_EXPECT(EnginesAgree<VubCrc16Xmodem>(20));
	SIM_EXPECT(EnginesAgree<VubCrc32IsoHdlc>(21));
	SIM_EXPECT(EnginesAgree<VubCrc32Bzip2>(22));
	SIM_EXPECT(EnginesAgree<VubCrc32C>(23));
	SIM_EXPECT(EnginesAgree<VubCrc32Mpeg2>(24));
	SIM_EXPECT(EnginesAgree<VubCrc32Posix>(25));
	SIM_EXPECT(EnginesAgree<VubCrc32Q>(26));
}


// Worked by hand. Modbus: the initial value FFFF, reflected, is FFFF; the final XOR is 0000; the result is FFFF. Usb: FFFF, reflected
// FFFF, XOR FFFF, is 0000. IsoHdlc: FFFFFFFF, reflected FFFFFFFF, XOR FFFFFFFF, is 00000000.
void TestEmptyMessages() {
	SIM_EXPECT(EmptyMessageGives<VubCrc16Modbus>(0xFFFF));
	SIM_EXPECT(EmptyMessageGives<VubCrc16Usb>(0x0000));
	SIM_EXPECT(EmptyMessageGives<VubCrc32IsoHdlc>(0x00000000));
}


// The table and the bitwise engine agree with the reference on random data for the custom set, which differs from the catalogue in its
// reflection and its initial value. The reference is the expected value, not the code under test.
bool CustomMatchesReference(uint32 seed) {
	using Algorithm = VubCrcAlgorithm<customParameters>;
	VubRandom random(seed);
	std::array<uint8, 64> data{};
	for (std::size_t round = 0; round < 300; round++) {
		std::size_t length = random.Below(65);
		for (std::size_t index = 0; index < length; index++) {
			data[index] = static_cast<uint8>(random.Next());
		}

		std::span<const uint8> bytes(data.data(), length);
		uint32 expected = ReferenceCrc(customParameters, bytes);
		if (VubCrcTable::Run<Algorithm>(bytes) != expected || VubCrcBitwise::Run<Algorithm>(bytes) != expected) {
			return false;
		}
	}

	return true;
}


void TestCustomParametersMatchReference() {
	SIM_EXPECT(CustomMatchesReference(30));
	SIM_EXPECT(CustomCrc::parameters.reflectIn != CustomCrc::parameters.reflectOut);
	SIM_EXPECT(customParameters.initialValue != 0);
	SIM_EXPECT(customParameters.initialValue != (VubCrcReverse(customParameters.initialValue, 16)));
}


// The custom set's transform, encoded and decoded by hand: the digits, then 4574 low byte first (74 45).
void TestCustomTransformEncodesAndDecodes() {
	CustomCrc transform;
	VubMeta meta{};
	std::array<uint8, 11> wire{};
	Result<std::size_t> encoded = transform.Encode(meta, std::span<const uint8>(checkDigits.data(), checkDigits.size()), wire);
	SIM_EXPECT(encoded.IsOk());
	const std::array<uint8, 11> expected = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x74, 0x45};
	SIM_EXPECT(wire == expected);
	Result<std::size_t> decoded = transform.Decode(meta, std::span<uint8>(wire.data(), wire.size()));
	SIM_EXPECT(decoded.IsOk());
	SIM_EXPECT(decoded.Value() == 9);
}


// A transform holds no state: the byte order is a type parameter, so no CRC transform has a member. Each is empty.
void TestTransformsHoldNoState() {
	SIM_EXPECT(std::is_empty_v<VubCrc16Modbus>);
	SIM_EXPECT(std::is_empty_v<VubCrc16ModbusHighByteFirst>);
	SIM_EXPECT((std::is_empty_v<VubCrc<VubCrc16Modbus::parameters, VubCrcBitwise>>));
	SIM_EXPECT(std::is_empty_v<CustomCrc>);
	SIM_EXPECT(std::is_empty_v<VubCrc8Smbus>);
}


void TestFingerprintsDifferByByteOrder() {
	SIM_EXPECT(VubCrc16Modbus::fingerprint != VubCrc16ModbusHighByteFirst::fingerprint);
	SIM_EXPECT((VubCrc16Modbus::fingerprint == VubCrc<VubCrc16Modbus::parameters, VubCrcBitwise>::fingerprint));
	SIM_EXPECT((VubCrc16ModbusHighByteFirst::fingerprint == VubCrcHighByteFirst<VubCrc16Modbus::parameters, VubCrcBitwise>::fingerprint));
}


// The table engine's table is the size the flash budget counts on: 256 entries of the CRC's width in bytes.
static_assert(sizeof(VubCrcTableOf<VubCrc8Smbus::parameters>::table) == 256);
static_assert(sizeof(VubCrcTableOf<VubCrc16Modbus::parameters>::table) == 512);
static_assert(sizeof(VubCrcTableOf<VubCrc32IsoHdlc::parameters>::table) == 1024);


int main() {
	TestPresetCheckValuesMatchReference();
	TestPresetsAtRunTime();
	TestEnginesAgreeOnRandomData();
	TestEmptyMessages();
	TestCustomParametersMatchReference();
	TestCustomTransformEncodesAndDecodes();
	TestTransformsHoldNoState();
	TestFingerprintsDifferByByteOrder();
	return SimFinish("TestVubCrcPresets");
}
