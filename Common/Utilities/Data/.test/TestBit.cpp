#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Data/Bit.h>

#include <cstdint>


// Host tests of the Bit helpers: the masks, the set, clear, toggle and check of single bits and of fields, on the narrow and the wide
// integer types, and the documented examples.


void ByteSetBuildsMask() {
	VHAL_CHECK_EQ(Bit::ByteSet<uint8>(0, 2, 4), static_cast<uint8>(0x15));
}


void SetClearToggleCheckUint8() {
	uint8 value = 0;
	Bit::Set(value, 3);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x08));
	VHAL_CHECK(Bit::Check(value, 3));
	Bit::Toggle(value, 3);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x00));
	Bit::Write(value, 7, true);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x80));
	Bit::Clear(value, 7);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x00));
}


void HighBitsOfWideTypes() {
	uint32 word = 0;
	Bit::Set(word, 31);
	VHAL_CHECK_EQ(word, 0x80000000u);
	VHAL_CHECK(Bit::Check(word, 31));

	uint64 wide = 0;
	Bit::Set(wide, 63);
	VHAL_CHECK_EQ(wide, 0x8000000000000000ull);
	VHAL_CHECK(Bit::Check(wide, 63));
	VHAL_CHECK(!Bit::Check(wide, 62));
}


void SetValueAndSetAtDocumentedExamples() {
	uint8 reg = 0;
	Bit::SetValue(reg, 3);
	VHAL_CHECK_EQ(reg, static_cast<uint8>(0x08));

	uint8 field = 0;
	Bit::SetAt(field, 0b11, 4);
	VHAL_CHECK_EQ(field, static_cast<uint8>(0x30));
}


void ClearAtDocumentedExample() {
	// Bit.h documents: 1111 1111 with ClearAt(value, 0b11, 4) becomes 1100 1111.
	uint8 value = 0xFF;
	Bit::ClearAt(value, 0b11, 4);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0xCF));
}


enum class Flag : uint8 {
	Low = 0,
	Mid = 3,
	High = 7
};


enum class WideFlag : uint64 {
	Bit40 = 40
};


// A bit given as an enum class value: set, check, toggle, clear, and the position of the high bit of the byte.
void SetClearToggleCheckEnumPosition() {
	uint8 value = 0;
	Bit::Set(value, Flag::Mid);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x08));
	VHAL_CHECK(Bit::Check(value, Flag::Mid));
	VHAL_CHECK(!Bit::Check(value, Flag::Low));

	Bit::Toggle(value, Flag::Mid);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x00));

	Bit::Set(value, Flag::High);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x80));
	Bit::Clear(value, Flag::High);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x00));

	Bit::SetValue(value, Flag::High);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x80));
	Bit::ClearAt(value, 0b1, Flag::High);
	VHAL_CHECK_EQ(value, static_cast<uint8>(0x00));
}


// A 64-bit enum position keeps all of its bits, so it addresses bit 40 of a uint64 and nothing else.
void EnumPositionOnWideValue() {
	uint64 wide = 0;
	Bit::Set(wide, WideFlag::Bit40);
	VHAL_CHECK_EQ(wide, 1ull << 40);
	VHAL_CHECK(Bit::Check(wide, WideFlag::Bit40));
	Bit::Toggle(wide, WideFlag::Bit40);
	VHAL_CHECK_EQ(wide, 0ull);
	Bit::Write(wide, WideFlag::Bit40, true);
	Bit::Clear(wide, WideFlag::Bit40);
	VHAL_CHECK_EQ(wide, 0ull);
}


int main() {
	ByteSetBuildsMask();
	SetClearToggleCheckUint8();
	HighBitsOfWideTypes();
	SetValueAndSetAtDocumentedExamples();
	ClearAtDocumentedExample();
	SetClearToggleCheckEnumPosition();
	EnumPositionOnWideValue();

	return VhalCheck::Finish("TestBit");
}
