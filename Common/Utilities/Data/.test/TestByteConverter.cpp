#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Data/ByteConverter.h>

#include <cstddef>
#include <cstdint>


// Host tests of ByteConverter: the type read in both byte orders, the reversal of arrays and integers, the bytes, halves and words, and
// the copies to and from byte arrays at an offset. Byte order follows significance, not the host memory layout.


void GetTypeEndianness() {
	uint8 bytes[4] = { 0x44, 0x33, 0x22, 0x11 };
	VHAL_CHECK_EQ(ByteConverter::GetType<uint32>(bytes), 0x11223344u);
	VHAL_CHECK_EQ(ByteConverter::GetType<uint32>(bytes, true), 0x44332211u);
}


void ReverseOddAndWideSizes() {
	uint8 three[3] = { 1, 2, 3 };
	ByteConverter::ReverseArray(three);
	VHAL_CHECK_EQ(three[0], static_cast<uint8>(3));
	VHAL_CHECK_EQ(three[1], static_cast<uint8>(2));
	VHAL_CHECK_EQ(three[2], static_cast<uint8>(1));

	uint64 value = 0x0102030405060708ull;
	ByteConverter::ReverseArray(value);
	VHAL_CHECK_EQ(value, 0x0807060504030201ull);
}


void GetSetByteAndHighLow() {
	uint32 word = 0xAABBCCDDu;
	VHAL_CHECK_EQ(ByteConverter::GetByte(word, 2), static_cast<uint8>(0xBB));
	VHAL_CHECK_EQ(ByteConverter::GetHigh(word), static_cast<uint16>(0xAABB));
	VHAL_CHECK_EQ(ByteConverter::GetLow(word), static_cast<uint16>(0xCCDD));

	uint16 half = 0xABCD;
	VHAL_CHECK_EQ(ByteConverter::GetHigh(half), static_cast<uint8>(0xAB));
	VHAL_CHECK_EQ(ByteConverter::GetLow(half), static_cast<uint8>(0xCD));
	VHAL_CHECK_EQ(ByteConverter::Create(static_cast<uint8>(0xAB), static_cast<uint8>(0xCD)), static_cast<uint16>(0xABCD));
	VHAL_CHECK_EQ(ByteConverter::Create(static_cast<uint16>(0xABCD), static_cast<uint16>(0x1234)), 0xABCD1234u);
}


void SetHighAndSetLowUint16() {
	uint16 value = 0xFF00;
	ByteConverter::SetLow(value, static_cast<uint8>(0x12));
	VHAL_CHECK_EQ(value, static_cast<uint16>(0xFF12));

	ByteConverter::SetHigh(value, static_cast<uint8>(0x34));
	VHAL_CHECK_EQ(value, static_cast<uint16>(0x3412));
}


void SetLowKeepsHighHalfUint32() {
	// SetLow(uint32, uint16) must replace only the low 16 bits.
	uint32 value = 0xAABBCCDDu;
	ByteConverter::SetLow(value, static_cast<uint16>(0x1234));
	VHAL_CHECK_EQ(value, 0xAABB1234u);
}


void SetHighKeepsLowHalfUint32() {
	uint32 value = 0xAABBCCDDu;
	ByteConverter::SetHigh(value, static_cast<uint16>(0x1234));
	VHAL_CHECK_EQ(value, 0x1234CCDDu);
}


void ToArrayCopyAtOffset() {
	// Writing 4 bytes at position 2 of a 64-byte array must land at indexes 2..5.
	uint8 array[64] = {};
	uint32 source = 0x11223344u;
	ByteConverter::ToArrayFromDataCopy(array, source, 2);
	VHAL_CHECK_EQ(array[2], static_cast<uint8>(0x44));
	VHAL_CHECK_EQ(array[3], static_cast<uint8>(0x33));
	VHAL_CHECK_EQ(array[4], static_cast<uint8>(0x22));
	VHAL_CHECK_EQ(array[5], static_cast<uint8>(0x11));
	VHAL_CHECK_EQ(array[16], static_cast<uint8>(0));
}


void ToDataFromArrayAtOffset() {
	uint8 array[8] = { 0, 0, 0x78, 0x56, 0x34, 0x12, 0, 0 };
	uint32 out = 0;
	ByteConverter::ToDataFromArrayCopy(out, array, 2);
	VHAL_CHECK_EQ(out, 0x12345678u);
	VHAL_CHECK_EQ(ByteConverter::FromArray<uint32>(array, 2), 0x12345678u);
}


void SetByteGetByteRoundTripsEveryIndex() {
	for (size_t index = 0; index < sizeof(uint32); index++) {
		uint32 value = 0;
		ByteConverter::SetByte(value, static_cast<uint8>(0xA0 + index), static_cast<uint8>(index));
		VHAL_CHECK_EQ(ByteConverter::GetByte(value, static_cast<uint8>(index)), static_cast<uint8>(0xA0 + index));
		VHAL_CHECK_EQ(value, static_cast<uint32>(static_cast<uint32>(0xA0 + index) << (8 * index)));
	}

	for (size_t index = 0; index < sizeof(uint64); index++) {
		uint64 value = 0;
		ByteConverter::SetByte(value, static_cast<uint8>(0xB0 + index), static_cast<uint8>(index));
		VHAL_CHECK_EQ(ByteConverter::GetByte(value, static_cast<uint8>(index)), static_cast<uint8>(0xB0 + index));
		VHAL_CHECK_EQ(value, static_cast<uint64>(static_cast<uint64>(0xB0 + index) << (8 * index)));
	}
}


void SetByteIndexZeroIsLeastSignificant() {
	uint32 value = 0;
	ByteConverter::SetByte(value, static_cast<uint8>(0x11), static_cast<uint8>(0));
	VHAL_CHECK_EQ(value, 0x00000011u);
	ByteConverter::SetByte(value, static_cast<uint8>(0x44), static_cast<uint8>(3));
	VHAL_CHECK_EQ(value, 0x44000011u);

	int32 negative = -1;
	ByteConverter::SetByte(negative, static_cast<uint8>(0x12), static_cast<uint8>(1));
	VHAL_CHECK_EQ(static_cast<uint32>(negative), 0xFFFF12FFu);
	VHAL_CHECK_EQ(ByteConverter::GetByte(negative, static_cast<uint8>(3)), static_cast<uint8>(0xFF));
}


int main() {
	GetTypeEndianness();
	ReverseOddAndWideSizes();
	GetSetByteAndHighLow();
	SetHighAndSetLowUint16();
	SetLowKeepsHighHalfUint32();
	SetHighKeepsLowHalfUint32();
	ToArrayCopyAtOffset();
	ToDataFromArrayAtOffset();
	SetByteGetByteRoundTripsEveryIndex();
	SetByteIndexZeroIsLeastSignificant();

	return VhalCheck::Finish("TestByteConverter");
}
