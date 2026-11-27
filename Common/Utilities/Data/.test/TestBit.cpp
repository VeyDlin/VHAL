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


int main() {
	ByteSetBuildsMask();
	SetClearToggleCheckUint8();
	HighBitsOfWideTypes();
	SetValueAndSetAtDocumentedExamples();
	ClearAtDocumentedExample();

	return VhalCheck::Finish("TestBit");
}
