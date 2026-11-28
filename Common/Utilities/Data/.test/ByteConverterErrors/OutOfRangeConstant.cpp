// expect: is outside the bounds of array type
#include <Utilities/Data/ByteConverter.h>
#include <array>


// A four-byte read from a two-byte array in a constant expression: the span form has no run-time check, so the compiler rejects it.
constexpr std::array<uint8, 2> bytes{};
constexpr uint32 value = ByteConverter::Get<uint32, ByteOrder::little>(bytes, 0);


int main() {
	return 0;
}
