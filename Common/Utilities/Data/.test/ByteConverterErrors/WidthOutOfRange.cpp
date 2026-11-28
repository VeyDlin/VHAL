// expect: a width is 1 to 8 bytes
#include <Utilities/Data/ByteConverter.h>


// A width form takes 1 to 8 bytes: a nine-byte width is rejected by the static_assert of the template.
int main() {
	uint8 bytes[9] = {};
	ByteConverter::Put<9, ByteOrder::little>(bytes, 0, 0);
	return 0;
}
