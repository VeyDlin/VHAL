#include <VHAL.h>
#include <VhalCheck.h>
#include <Utilities/Data/ByteConverter.h>

#include <array>
#include <cstddef>
#include <utility>


// Host tests of the byte order forms of ByteConverter: every form in both orders, every width from 1 to 8 at compile time and at
// run time, signed and enum value types, truncation, offsets, and the round trips. The constant expressions at the top are checked
// when this file compiles.


enum class Mode : uint8 {
	idle = 0x01,
	run = 0xA5
};


enum class Offset : int32 {
	below = -2
};


constexpr uint64 pattern = 0x0102030405060708ull;


constexpr std::array<uint8, 8> FilledBigAndLittle() {
	std::array<uint8, 8> bytes{};
	ByteConverter::Put<uint32, ByteOrder::big>(bytes, 0, 0x11223344u);
	ByteConverter::Put<uint32, ByteOrder::little>(bytes, 4, 0x11223344u);
	return bytes;
}


// Constant expressions: the big-endian word first, then the little-endian one.
static_assert(FilledBigAndLittle()[0] == 0x11 && FilledBigAndLittle()[3] == 0x44);
static_assert(FilledBigAndLittle()[4] == 0x44 && FilledBigAndLittle()[7] == 0x11);
static_assert(ByteConverter::Get<uint32, ByteOrder::big>(FilledBigAndLittle(), 0) == 0x11223344u);
static_assert(ByteConverter::Get<uint32, ByteOrder::little>(FilledBigAndLittle(), 4) == 0x11223344u);
static_assert(ByteConverter::Get<int16, ByteOrder::big>(std::array<uint8, 2>{ 0xFF, 0xFE }, 0) == -2);
static_assert(ByteConverter::Get<int16, ByteOrder::little>(std::array<uint8, 2>{ 0xFE, 0xFF }, 0) == -2);
static_assert(ByteConverter::Get<Mode, ByteOrder::big>(std::array<uint8, 1>{ 0xA5 }, 0) == Mode::run);
static_assert(ByteConverter::Get<Offset, ByteOrder::little>(std::array<uint8, 4>{ 0xFE, 0xFF, 0xFF, 0xFF }, 0) == Offset::below);
static_assert(ByteConverter::Get<3, ByteOrder::big>(FilledBigAndLittle(), 0) == 0x112233u);
static_assert(ByteConverter::Get<ByteOrder::little>(FilledBigAndLittle(), 4, 4) == 0x11223344u);
static_assert(ByteConverter::FromBytes<uint16, ByteOrder::big>(std::array<uint8, 2>{ 0x12, 0x34 }) == 0x1234);
static_assert(ByteConverter::FromBytes<uint16, ByteOrder::little>(std::array<uint8, 2>{ 0x12, 0x34 }) == 0x3412);
static_assert(ByteConverter::ToBytes<uint16, ByteOrder::big>(0x1234)[0] == 0x12);
static_assert(ByteConverter::ToBytes<uint16, ByteOrder::little>(0x1234)[0] == 0x34);
static_assert(ByteConverter::ToBytes<int32, ByteOrder::big>(-1)[3] == 0xFF);


// Every width from 1 to 8, in both orders: the template and the run-time width forms write the same bytes, and the template Get and
// the run-time Get read the value back.
template<ByteOrder order, std::size_t width>
constexpr bool WidthFormsAgree() {
	const uint64 value = pattern >> (8 * (8 - width));
	std::array<uint8, 8> typed{};
	std::array<uint8, 8> runtime{};
	ByteConverter::Put<width, order>(typed, 0, value);
	ByteConverter::Put<order>(runtime, 0, value, width);
	for (std::size_t index = 0; index < width; index++) {
		if (typed[index] != runtime[index]) {
			return false;
		}
	}
	return ByteConverter::Get<width, order>(typed, 0) == value && ByteConverter::Get<order>(runtime, 0, width) == value;
}


static_assert(WidthFormsAgree<ByteOrder::big, 1>());
static_assert(WidthFormsAgree<ByteOrder::big, 2>());
static_assert(WidthFormsAgree<ByteOrder::big, 3>());
static_assert(WidthFormsAgree<ByteOrder::big, 4>());
static_assert(WidthFormsAgree<ByteOrder::big, 5>());
static_assert(WidthFormsAgree<ByteOrder::big, 6>());
static_assert(WidthFormsAgree<ByteOrder::big, 7>());
static_assert(WidthFormsAgree<ByteOrder::big, 8>());
static_assert(WidthFormsAgree<ByteOrder::little, 1>());
static_assert(WidthFormsAgree<ByteOrder::little, 2>());
static_assert(WidthFormsAgree<ByteOrder::little, 3>());
static_assert(WidthFormsAgree<ByteOrder::little, 4>());
static_assert(WidthFormsAgree<ByteOrder::little, 5>());
static_assert(WidthFormsAgree<ByteOrder::little, 6>());
static_assert(WidthFormsAgree<ByteOrder::little, 7>());
static_assert(WidthFormsAgree<ByteOrder::little, 8>());


// A value written in one order and read back in the same order is the value.
void RoundTripSignedAndEnum() {
	uint8 bytes[8] = {};

	ByteConverter::Put<int8, ByteOrder::big>(bytes, 0, static_cast<int8>(-3));
	VHAL_CHECK_EQ((ByteConverter::Get<int8, ByteOrder::big>(bytes, 0)), static_cast<int8>(-3));

	ByteConverter::Put<int16, ByteOrder::little>(bytes, 0, static_cast<int16>(-300));
	VHAL_CHECK_EQ((ByteConverter::Get<int16, ByteOrder::little>(bytes, 0)), static_cast<int16>(-300));

	ByteConverter::Put<int32, ByteOrder::big>(bytes, 0, static_cast<int32>(-70000));
	VHAL_CHECK_EQ((ByteConverter::Get<int32, ByteOrder::big>(bytes, 0)), static_cast<int32>(-70000));

	ByteConverter::Put<int64, ByteOrder::little>(bytes, 0, static_cast<int64>(-5000000000));
	VHAL_CHECK_EQ((ByteConverter::Get<int64, ByteOrder::little>(bytes, 0)), static_cast<int64>(-5000000000));

	ByteConverter::Put<Mode, ByteOrder::little>(bytes, 2, Mode::run);
	VHAL_CHECK_EQ(bytes[2], static_cast<uint8>(0xA5));
	VHAL_CHECK((ByteConverter::Get<Mode, ByteOrder::little>(bytes, 2) == Mode::run));

	ByteConverter::Put<Offset, ByteOrder::big>(bytes, 0, Offset::below);
	VHAL_CHECK((ByteConverter::Get<Offset, ByteOrder::big>(bytes, 0) == Offset::below));
	VHAL_CHECK_EQ(bytes[0], static_cast<uint8>(0xFF));
	VHAL_CHECK_EQ(bytes[3], static_cast<uint8>(0xFE));
}


// The 64-bit pattern in both orders: big-endian writes 01 first, little-endian writes 08 first.
void FullWordBothOrders() {
	uint8 bytes[8] = {};

	ByteConverter::Put<uint64, ByteOrder::big>(bytes, 0, pattern);
	for (std::size_t index = 0; index < 8; index++) {
		VHAL_CHECK_EQ(bytes[index], static_cast<uint8>(index + 1));
	}
	VHAL_CHECK_EQ((ByteConverter::Get<uint64, ByteOrder::big>(bytes, 0)), pattern);

	ByteConverter::Put<uint64, ByteOrder::little>(bytes, 0, pattern);
	for (std::size_t index = 0; index < 8; index++) {
		VHAL_CHECK_EQ(bytes[index], static_cast<uint8>(8 - index));
	}
	VHAL_CHECK_EQ((ByteConverter::Get<uint64, ByteOrder::little>(bytes, 0)), pattern);
}


// The pointer and span forms write at an offset and leave the bytes around the value alone.
void OffsetAndNeighbours() {
	uint8 bytes[10] = {};
	ByteConverter::Put<uint16, ByteOrder::little>(bytes, 3, static_cast<uint16>(0xABCD));
	VHAL_CHECK_EQ(bytes[2], static_cast<uint8>(0));
	VHAL_CHECK_EQ(bytes[3], static_cast<uint8>(0xCD));
	VHAL_CHECK_EQ(bytes[4], static_cast<uint8>(0xAB));
	VHAL_CHECK_EQ(bytes[5], static_cast<uint8>(0));

	std::array<uint8, 10> array{};
	ByteConverter::Put<uint32, ByteOrder::big>(array, 5, 0x01020304u);
	VHAL_CHECK_EQ(array[4], static_cast<uint8>(0));
	VHAL_CHECK_EQ(array[5], static_cast<uint8>(0x01));
	VHAL_CHECK_EQ(array[8], static_cast<uint8>(0x04));
	VHAL_CHECK_EQ(array[9], static_cast<uint8>(0));
	VHAL_CHECK_EQ((ByteConverter::Get<uint32, ByteOrder::big>(array, 5)), 0x01020304u);

	const uint8* constBytes = bytes;
	VHAL_CHECK_EQ((ByteConverter::Get<uint16, ByteOrder::little>(constBytes, 3)), static_cast<uint16>(0xABCD));
	VHAL_CHECK_EQ((ByteConverter::Get<uint16, ByteOrder::little>(std::span<const uint8>(bytes), 3)), static_cast<uint16>(0xABCD));
}


// Writing a value wider than the width keeps the low bytes only.
void WidthTruncatesTheValue() {
	uint8 bytes[4] = { 0xEE, 0xEE, 0xEE, 0xEE };
	ByteConverter::Put<2, ByteOrder::big>(bytes, 0, 0xFFFF1234ull);
	VHAL_CHECK_EQ(bytes[0], static_cast<uint8>(0x12));
	VHAL_CHECK_EQ(bytes[1], static_cast<uint8>(0x34));
	VHAL_CHECK_EQ(bytes[2], static_cast<uint8>(0xEE));

	ByteConverter::Put<ByteOrder::little>(bytes, 0, 0xFFFF1234ull, 2);
	VHAL_CHECK_EQ(bytes[0], static_cast<uint8>(0x34));
	VHAL_CHECK_EQ(bytes[1], static_cast<uint8>(0x12));
	VHAL_CHECK_EQ(bytes[2], static_cast<uint8>(0xEE));
}


// The width of a template and of a run-time argument give the same bytes and the same value for every width from 1 to 8.
template<std::size_t width>
void CheckOneWidth() {
	const uint64 value = pattern >> (8 * (8 - width));

	uint8 templateBytes[8] = {};
	uint8 runtimeBytes[8] = {};
	ByteConverter::Put<width, ByteOrder::big>(templateBytes, 0, value);
	ByteConverter::Put<ByteOrder::big>(runtimeBytes, 0, value, width);
	for (std::size_t index = 0; index < width; index++) {
		VHAL_CHECK_EQ(templateBytes[index], runtimeBytes[index]);
		VHAL_CHECK_EQ(templateBytes[index], static_cast<uint8>(value >> (8 * (width - 1 - index))));
	}
	VHAL_CHECK_EQ((ByteConverter::Get<width, ByteOrder::big>(templateBytes, 0)), value);
	VHAL_CHECK_EQ(ByteConverter::Get<ByteOrder::big>(runtimeBytes, 0, width), value);

	ByteConverter::Put<width, ByteOrder::little>(templateBytes, 0, value);
	ByteConverter::Put<ByteOrder::little>(runtimeBytes, 0, value, width);
	for (std::size_t index = 0; index < width; index++) {
		VHAL_CHECK_EQ(templateBytes[index], runtimeBytes[index]);
		VHAL_CHECK_EQ(templateBytes[index], static_cast<uint8>(value >> (8 * index)));
	}
	VHAL_CHECK_EQ((ByteConverter::Get<width, ByteOrder::little>(templateBytes, 0)), value);
	VHAL_CHECK_EQ(ByteConverter::Get<ByteOrder::little>(runtimeBytes, 0, width), value);
}


template<std::size_t... widths>
void CheckEveryWidth(std::index_sequence<widths...> /* sequence */) {
	(CheckOneWidth<widths + 1>(), ...);
}


// The typed form and the width form of the same width write the same bytes.
void TypedAndWidthFormsAgree() {
	uint8 typed[8] = {};
	uint8 width[8] = {};
	ByteConverter::Put<uint32, ByteOrder::little>(typed, 0, 0xAABBCCDDu);
	ByteConverter::Put<4, ByteOrder::little>(width, 0, 0xAABBCCDDull);
	for (std::size_t index = 0; index < 4; index++) {
		VHAL_CHECK_EQ(typed[index], width[index]);
	}
}


// ToBytes and FromBytes: the array holds the bytes in the order named by the type, and FromBytes reads them back.
void ToBytesAndFromBytes() {
	std::array<uint8, 2> big = ByteConverter::ToBytes<uint16, ByteOrder::big>(static_cast<uint16>(0x1234));
	VHAL_CHECK_EQ(big[0], static_cast<uint8>(0x12));
	VHAL_CHECK_EQ(big[1], static_cast<uint8>(0x34));
	VHAL_CHECK_EQ((ByteConverter::FromBytes<uint16, ByteOrder::big>(big)), static_cast<uint16>(0x1234));

	std::array<uint8, 2> little = ByteConverter::ToBytes<uint16, ByteOrder::little>(static_cast<uint16>(0x1234));
	VHAL_CHECK_EQ(little[0], static_cast<uint8>(0x34));
	VHAL_CHECK_EQ(little[1], static_cast<uint8>(0x12));
	VHAL_CHECK_EQ((ByteConverter::FromBytes<uint16, ByteOrder::little>(little)), static_cast<uint16>(0x1234));

	std::array<uint8, 4> negative = ByteConverter::ToBytes<int32, ByteOrder::little>(static_cast<int32>(-2));
	VHAL_CHECK_EQ(negative[0], static_cast<uint8>(0xFE));
	VHAL_CHECK_EQ((ByteConverter::FromBytes<int32, ByteOrder::little>(negative)), static_cast<int32>(-2));

	VHAL_CHECK((ByteConverter::FromBytes<Mode, ByteOrder::big>(ByteConverter::ToBytes<Mode, ByteOrder::big>(Mode::run)) == Mode::run));
}


int main() {
	RoundTripSignedAndEnum();
	FullWordBothOrders();
	OffsetAndNeighbours();
	WidthTruncatesTheValue();
	CheckEveryWidth(std::make_index_sequence<8>());
	TypedAndWidthFormsAgree();
	ToBytesAndFromBytes();

	return VhalCheck::Finish("TestByteConverterOrder");
}
